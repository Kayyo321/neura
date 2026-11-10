# `Std.Collections.StringMap`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.StringMap`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.StringMap`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **StringMap** · [`export let StringMap = class<V> implements MutableMap<string, V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L22)
  A mutable hash table that visits entries in insertion order.
- **StringMap.StringMap** · [`export let init = func(seal: Seal, table: hash_core.HashCore)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L33)
  Initializes a map over the empty table supplied by the module factory.
- **StringMap.StringMap** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L38)
  Returns the number of entries.
- **StringMap.StringMap** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L41)
  Returns true when there are no entries.
- **StringMap.StringMap** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L44)
  Removes all entries, resets the table to its original size, and invalidates iterators.
- **StringMap.StringMap** · [`export let contains_key = func(key: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L51)
  Reports whether an entry exists for `key`.
- **StringMap.StringMap** · [`export let get = func(key: string) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L54)
  Returns the associated value as `Some`, or `None` when the key is absent.
- **StringMap.StringMap** · [`export let get_or = func(key: string, fallback: V) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L63)
  Returns the associated value, or `fallback` when the key is absent.
- **StringMap.StringMap** · [`export let require = func(key: string) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L72)
  Returns the associated value. Traps with `no such element` when the key is absent.
- **StringMap.StringMap** · [`export let set = func(key: string, value: V)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L81)
  Stores the value for `key`. Replacing an existing value preserves the key and iterator validity.
- **StringMap.StringMap** · [`export let put = func(key: string, value: V) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L92)
  Stores the value for `key` and returns its previous value as `Some`, or `None` for a new key.
- **StringMap.StringMap** · [`export let delete = func(key: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L106)
  Removes the entry for `key`. Returns true if an entry was present.
- **StringMap.StringMap** · [`export let remove = func(key: string) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L116)
  Removes the entry for `key` and returns its value as `Some`, or `None` when absent.
- **StringMap.StringMap** · [`export let keys = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L127)
  Copies the keys to a new built-in list in insertion order.
- **StringMap.StringMap** · [`export let values = func -> V[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L142)
  Copies the values to a new built-in list in insertion order.
- **StringMap.StringMap** · [`export let entries = func -> [string, V][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L157)
  Copies the `[key, value]` tuples to a new built-in list in insertion order.
- **StringMap.StringMap** · [`export let iterator = func -> Iterator<[string, V]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L173)
  Returns a fail-fast iterator over `[key, value]` tuples in insertion order.
- **StringMap.StringMap** · [`export let put_all = func(other: StringMap<V>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L177)
  Sets each entry of `other` in its iteration order.
- **StringMap.StringMap** · [`export let for_each = func(action: func(string, V) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L190)
  Calls `action` with the key and value of each entry in insertion order. The action leaves the map structure unchanged.
- **StringMap.StringMap** · [`export let remove_if = func(drop: func(string, V) -> bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L207)
  Calls `drop` once per entry in insertion order, then removes the matches and compacts at most once. The predicate leaves the map structure unchanged. The number of removed entries.
- **StringMap.StringMap** · [`export let get_or_put = func(key: string, make: func() -> V) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L239)
  Returns the associated value, or calls `make` once, stores its result, and returns it when absent. The factory leaves the map structure unchanged.
- **StringMap.StringMap** · [`export let merge = func(key: string, value: V, combine: func(V, V) -> V) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L256)
  Stores and returns `value` when the key is absent. Otherwise stores and returns `combine(old, value)`. The combiner leaves the map structure unchanged.
- **StringMap.StringMap** · [`export let compute = func(key: string, f: func(string, optional.Optional<V>) -> optional.Optional<V>) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L276)
  Calls `f` with the key and its optional current value. Stores a `Some` result or removes the entry for `None`. The callback leaves the map structure unchanged. The result of the callback.
- **StringMap.StringMap** · [`export let contains_value = func(value: V) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L306)
  Tests whether `value.equals(held_value)` is true for any entry.
- **StringMap.StringMap** · [`export let trim = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L320)
  Compacts to the smallest table that holds the live entries and invalidates iterators.
- **StringMap.StringMap** · [`export let validate = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L326)
  Traps with "invalid state: ..." on a broken table invariant or when a live key's hash differs from its cached hash.
- **StringMap.StringMap** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L343)
  Another StringMap with the same size in which, for each of its entries, this map holds an equal value for the key.
- **StringMap.StringMap** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L367)
  The wrapping sum over entries of the key hash exclusive-or the value hash.
- **StringMap.StringMap** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L384)
  Formats entries as `{k: v, k2: v2}` in iteration order. The map itself as a direct key or value prints "(this collection)".
- **StringMap** · [`export let create = func<V> -> StringMap<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L517)
  Creates an empty map with the default table size.
- **StringMap** · [`export let with_capacity = func<V>(capacity: int) -> StringMap<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/string-map.nra#L521)
  Creates an empty map sized for `capacity` entries. Traps with `invalid argument: capacity` outside 0 through 2^29.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 9.1 Contracts

This section fixes the member sets that the concrete classes and the interfaces in [section 9.10](Stdlib-Design#910-interfaces-and-read-only-views) share.

The **Needs** column names the compiler facilities an implementation relies on.

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

### 9.6 Hash collections

**Decisions kept from the first plan:** hash collections iterate in **insertion order**, so output never depends on hash values, and the load factor is fixed at one half.

The index and entry tables are built-in lists grown by `append`. The shared probing core uses indices and hash values. Constructors can use specialized default value members or accept caller-supplied key operations.

#### Layout comparison

| Layout | Storage operations per successful lookup | Objects per entry | Removed entry released | Verdict |
| --- | --- | --- | --- | --- |
| Parallel lists: an integer index plus dense lists of hashes, keys, and values | Index read, hash read, key read, value read | None | At the next compaction, which is bounded (below) | **Chosen.** Probing touches only integers, insertion order is free, and nothing is allocated per entry. |
| One list of entry objects, with a shared empty object in removed slots | Index read, entry read, then a field read for each of hash, key, and value | One | Immediately | One more operation on every probe that reaches an entry, and an allocation per insertion. |
| Chained bucket nodes | A bucket read, then per node a field read for hash, key, and next | One | Immediately | Needs a second structure to keep insertion order. |

#### `HashCore`

`Std.Collections.Internal.HashCore` is a **non-generic** class. It knows hashes and entry numbers. It never sees a key. The owning collection keeps the keys and values in its own dense lists, indexed by entry number, and does the key comparison itself. One core therefore serves every hash collection, including the ones that can be written today.

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
| `IntMap<V>` | `int` | `Int32.hash` and `==`, called directly | Nothing: it can be written today |
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

### 13.1 `Std.Encoding`

Everything here needs T1 except the `Json` value type itself.

- **`Utf8`:** `encode(s)` (a built-in `uint8` list); `decode(bytes) → Result<string>`; `decode_lossy(bytes) → string`; `validate(bytes) → int` (the offset of the first invalid byte, or -1). `validate` works on bytes alone and can be written today.
- **`Hex`:** `encode(bytes) → string` (lowercase); `decode(text) → Result<uint8[list]>` (either case; an odd length or a non-hex character fails with an `Encoding` error).
- **`Base64`:** the standard and URL-safe alphabets of RFC 4648. `encode`, `encode_url`, and their `_unpadded` forms; `decode` and `decode_url`. Decoding is strict: padded forms require correct padding, unpadded forms reject padding, whitespace is rejected, and unused trailing bits must be zero.
- **`Csv`** (RFC 4180): `parse(text)` returns a `Result` of a built-in list of built-in lists of strings, and `write(rows) → string`. Fields are separated by commas and records by LF or CRLF. A quoted field may contain commas, line breaks, and doubled quotes. A quote inside an unquoted field, or text after a closing quote, fails with a `Parse` error. A final line break does not add an empty record. The writer quotes a field only when it contains a comma, a quote, a CR, or an LF, and ends each record with LF.

**`Json`** (RFC 8259).

**Recommendation: `JsonValue` is a recursive payload enum.** The first plan had an interface, six implementing classes, a kind enum, and five accessors returning `Optional`. All of that is one declaration and `match`, and it compiles today: the probe declared this shape and walked it with mutually recursive functions in both build modes.

| Variant | Payload |
| --- | --- |
| `Null` | None |
| `Boolean` | `bool` |
| `Number` | `string`: the number's JSON text |
| `Text` | `string` |
| `Array` | A built-in list of `JsonValue` |
| `Object` | A built-in list of `[string, JsonValue]`, in member order |

- **Objects are member lists.** They keep insertion order in the serialized representation. Lookup is a linear scan. `Json.index(members)` builds a `StringMap<JsonValue>` when callers need repeated lookups.
- **Numbers keep their text**, so no precision is lost in a round trip. The payload must be valid JSON number text; the writer checks it and traps with `invalid argument: number text` otherwise.
- **Aliasing.** The payloads of `Array` and `Object` are built-in lists: they are mutable, and the same list can be reached from more than one value. A `JsonValue` is therefore not a frozen tree. Changing a list changes every value that holds it, and a list can be made to contain a value that holds the same list. `write`, `equals`, and `hash` do not detect such a cycle and end with the frame-limit fault. The parser always produces a tree with no sharing.

The module `Json`:

| Function | Behavior | Needs |
| --- | --- | --- |
| `number_of_int64(v: int64) → JsonValue` | A `Number` with the integer's text. | Today |
| `number_of_float64(v: float64) → JsonValue` | A `Number` with the float's text. NaN and infinities trap. | N1 |
| `member(members, name: string) → Optional<JsonValue>` | The first member with that name. | T1 |
| `number_to_int64(text: string) → Optional<int64>` | `Some` when the text has no fraction or exponent and fits. | T1 |
| `number_to_float64(text: string) → float64` | The nearest value. | T1, N1 |
| `number_to_big_decimal(text: string) → BigDecimal` | Exact. | T1 |
| `parse(text: string) → Result<JsonValue>` | Below. | T1 |
| `write(value) → string`, `write_pretty(value, indent: int) → string` | Below. | T1 |
| `equals(a, b) → bool`, `hash(value) → uint32` | Structural. Numbers are equal when their exact decimal values are. Objects are equal when they have the same number of members and each member of one has an equal value under the same name in the other, regardless of order. | T1 |

- **Parsing** is strict recursive descent: no comments, no trailing commas, no leading zeros, only the four JSON whitespace characters. String escapes include `\uXXXX`, with surrogate pairs combined; an unpaired surrogate fails. Nesting deeper than 512 fails. For a repeated member name, the last value wins and the name keeps its first position. Anything but whitespace after the value fails. Errors are `Parse` errors with the byte offset.
- **Writing.** `write` produces no whitespace. `write_pretty` puts each element and member on its own line, indented by `indent` spaces per level, with `: ` after names; empty arrays and objects are `[]` and `{}`. Strings escape `"`, `\`, and characters below 0x20 (using `\b`, `\f`, `\n`, `\r`, `\t`, and otherwise `\u00XX` with lowercase hex). Nothing else is escaped. Every member of an object is written, including repeated names.

### 18.7 Encodings and digests

- **Additions.** `Crc32.create()` and `Sha256.create()` make fresh digests; `Base64.decode_unpadded` and `decode_url_unpadded` read the unpadded forms, which reject padding.
- **Utf8** delegates to `Strings`, so its errors match `Strings.from_bytes`; `validate` returns the length of the longest valid prefix, the offset `decode` reports.
- **Hex errors.** `invalid hex digit` at the byte; `odd number of hex digits` at the text length, reported only when every digit is valid.
- **Base64 errors.** `invalid character`, `invalid padding`, and `unexpected padding` at the byte; `unexpected end of data` at the text length; `nonzero trailing bits` at the last character. The first structural error wins.
- **Csv.** A CR that does not start a CRLF is data. An empty line is a record with one empty field, and a record with no fields is written as an empty line. Errors: `quote in unquoted field` and `text after closing quote` (Syntax), `unterminated quoted field` (UnexpectedEnd).
- **Json parse errors.** `Empty` for empty or all-whitespace text; `UnexpectedEnd` at the text length; `Syntax` for structure; `InvalidCharacter` inside a token, with escapes reported at the backslash; `OutOfRange` at the bracket that opens level 513. Top-level scalars are accepted.
- **Json values.** Numbers compare by a canonical decimal text, including exponents beyond `int64`. `hash` starts each variant from its position (Null 1 through Object 6), and an object's hash combines its member count with the wrapping sum of its distinct member hashes, so it agrees with order-insensitive equality even when names repeat. `number_to_int64` is `None` for anything but a plain integer. `index` keeps the last value of a repeated name. A `StringMap<JsonValue>` supports lookups; its `equals` and `to_string` are rejected at build time because `JsonValue` has no built-in value members.
- **Json traps.** `number is not finite`, `negative indent`, `number text`, and `number exponent out of range`, after `invalid argument: `.
- **Sha256** keeps rolled loops: unrolling cut steps by about four but grew the image by 1.5 to 3.6 MB.

### 18.9 Hash collections, trees, and sequences

- **Compaction rule 1** also compacts when live entries plus tombstones fill `E`. Trailing releases leave tombstones behind, so without this addition the index could fill and a lookup for a missing key would not terminate.
- **Modification counts are `uint64`**, as [section 5.4](Stdlib-Design#54-iteration) states; compaction and `trim()` advance them, so an iterator traps after either. `clear()` returns to the table size chosen at construction.
- **Bulk removal.** `remove_if` and `retain_if` call the callback on every entry, then release the chosen entries and compact at most once. `compute` returns the result of its callback. A `with_keys` collection hashes with its stored function, so `hash_code` agrees with its equality.
- **IntMap and StringMap** are generated from `hash-map.nra` with the key operations called directly (`tests/stdlib/hash-map/generators/gen_maps.py`); `StringMap` uses the string's built-in `hash_code` and `==`, which equal `Strings.hash` and `Strings.equals`, because `Std.Text` is above `Std.Collections` in the dependency order.
- **Extra members.** `with_keys_and_capacity`; `trim` and `validate` on every hash collection; `TreeMap.key_iterator`, `key_range`, and `descending_keys`, which `TreeSet` uses to avoid a tuple per element; `Sequence.from_iterator`, a public `Sequence` constructor from two functions, and `Sequence.iterator()` so that a sequence works once in for-each. Constructors of these collections take a module-private token, so only their module functions build them.
- **Sequence choices.** `range(from, to)` on a sorted collection is empty when `to` is not above `from`. `windowed` emits only full windows. `zip` and `chain` mark the other sequence used. `from_list` fails fast on a structural change to its list. `sum_float64` starts from the first element, so `[-0.0]` sums to `-0.0`, and an empty sequence sums to `0.0`. Maps and sets have no `sequence()` member; `ArrayList` and `LinkedList` build theirs with `from_iterator`.
- **Trap details.** `duplicate key`, `negative count`, `chunk size`, and `window size` after `invalid argument: `; `invalid state: hash table …` and `invalid state: tree …` for a failed `validate()`.

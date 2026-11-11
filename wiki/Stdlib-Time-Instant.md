# `Std.Time.Instant`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Time.Instant`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Time.Instant`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Instant** · [`export let Instant = class implements Comparable<Instant>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L20)
  An immutable point on the UTC timeline, normalized to whole seconds and a nonnegative fraction.
- **Instant.Instant** · [`export let init = func(epoch_seconds: int64, nanos: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L27)
  Stores whole seconds and an already-normalized nanosecond adjustment.
- **Instant.Instant** · [`export let epoch_seconds = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L33)
  Returns the whole-second offset from the Unix epoch.
- **Instant.Instant** · [`export let nanos = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L36)
  0 through 999,999,999.
- **Instant.Instant** · [`export let to_epoch_millis = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L39)
  Milliseconds since the epoch, rounded toward negative infinity.
- **Instant.Instant** · [`export let plus = func(span: duration.Duration) -> Instant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L49)
  Adds a duration and normalizes the nanosecond fraction, borrowing or carrying as needed.
- **Instant.Instant** · [`export let minus = func(span: duration.Duration) -> Instant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L60)
  Subtracts a duration without negating it first, then normalizes the fraction.
- **Instant.Instant** · [`export let duration_since = func(other: Instant) -> duration.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L71)
  This instant minus `other`. Traps (reason 1) when the difference does not fit a Duration.
- **Instant.Instant** · [`export let to_local = func(offset: zone_offset.ZoneOffset) -> local_date_time.LocalDateTime`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L88)
  Traps with "invalid argument: date out of range" when the local date is outside years -999,999 through 999,999.
- **Instant.Instant** · [`export let compare_to = func(other: Instant) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L97)
  Orders instants by epoch second and then by nanosecond adjustment.
- **Instant.Instant** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L114)
  Tests equality of epoch seconds and nanosecond adjustments.
- **Instant.Instant** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L123)
  Combines the hashes of the seconds and nanosecond adjustment.
- **Instant.Instant** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L130)
  YYYY-MM-DDTHH:MM:SS, a fraction of 3, 6, or 9 digits (the shortest that is exact) when the nanoseconds are not zero, then Z. Years outside 0000 through 9999 have a sign and at least four digits; every int64 second count has a text.
- **Instant** · [`export const Epoch: Instant = new Instant(0, 0)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L139)
  The Unix epoch, 1970-01-01T00:00:00Z.
- **Instant** · [`export let of_epoch_seconds = func(seconds: int64, nanos: int) -> Instant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L143)
  `nanos` may be any int: whole seconds in it move into the seconds, and a negative remainder borrows one.
- **Instant** · [`export let of_epoch_millis = func(millis: int64) -> Instant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L150)
  Converts a signed millisecond count into normalized epoch seconds and nanoseconds.
- **Instant** · [`export let parse = func(text: string) -> result.Result<Instant>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/instant.nra#L160)
  The to_string form with a fraction of one through nine digits, and Z or a numeric offset (+HH:MM or -HH:MM, at most 18:00) after the time. Years are -999,999 through 999,999. Errors are those of LocalDate.parse and LocalTime.parse for the date and time, then those of ZoneOffset.parse for the offset.
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

### 11.2 `Std.Fs`

Needs P2 for every host operation and T1 for `Path`.

**`Path`** is an immutable class holding normalized text.

- The separator is `/` on every host. The host service translates.
- **Normalization at construction:** repeated separators collapse; `.` components are removed; a trailing separator is removed except for the root; an empty result is `.`. `..` components are kept, because removing them is not correct in the presence of links.
- `is_absolute()` is true exactly when the text starts with `/`. Platform prefixes such as drive letters are passed to the host unchanged and are not interpreted.
- Module function `of(text)`. Members include `join(other: string) → Path`, `parent() → Path`, `file_name()`, `stem()`, `extension()`, `components()`, `resolve_lexically() → Path`, `starts_with(other: Path) → bool`, and `relative_to(base: Path) → Path`. An absolute argument to `join` replaces the path. `parent` returns `nil` when the path has no parent. `relative_to` returns `nil` when it cannot express the path relative to the base. `file_name`, `stem`, and `extension` return `Optional<string>`. The extension is the text after the last `.` of the file name when that dot is not the first character. `components` returns a built-in list of strings. Equality, hash, ordering, and text use the normalized path.

**`File`** is a class wrapping a host file ID. The module functions `open(path, mode: OpenMode) → Result<File>` and `with_open<T>(path, mode, body)`, the scoped helper of [section 5.5](Stdlib-Design#55-resources). `OpenMode` is a fieldless enum: `Read` (must exist), `Write` (create or truncate), `Append`, `ReadWrite` (must exist), `CreateNew` (fails with `AlreadyExists`). Members: `read` (a `ByteSource` when bound), `write` (a `ByteSink` when bound), `seek(offset: int64, origin: SeekOrigin) → Result<int64>` (`SeekOrigin`: `Start`, `Current`, `End`), `size() → Result<int64>`, and `close() → Result<Unit>` ([section 5.5](Stdlib-Design#55-resources)). A `File` is unbuffered.

**`Fs`** module:

| Member | Behavior |
| --- | --- |
| `read_bytes(path) → Result<uint8[list]>`, `read_text(path) → Result<string>` | `read_text` fails with `InvalidData` for invalid UTF-8. Both close the file on every path, including a failed read. |
| `write_bytes(path, bytes)`, `write_text(path, text)`, `append_text(path, text)` `→ Result<Unit>` | Create the file if needed; the `write_` forms truncate. A failed write is reported after the file is closed. |
| `exists(path) → Result<bool>` | `Ok(false)` for `NotFound`; other failures are errors. |
| `metadata(path) → Result<FileMetadata>` | `FileMetadata` is a class: `kind` (`FileKind`: `File`, `Directory`, `Other`), `size: int64`, `modified: Instant`. |
| `list(path)` | A `Result` of a built-in list of `Path`: the directory's entries joined to `path`, sorted by name bytes. |
| `create_directory(path)`, `create_directories(path)` | The second creates missing parents and succeeds when the directory exists. |
| `remove_file(path)`, `remove_directory(path)` | `remove_directory` requires an empty directory. |
| `remove_tree(path)` | Removes depth first, in sorted order, without descending into an entry of kind `Other`. Stops at the first error. |
| `rename(from, to)`, `copy_file(from, to)` | `copy_file` streams through a 65,536-byte buffer, replaces an existing target, and closes both files on every path. |

"On every path" is written out in each function as explicit `match` arms that close before returning the error. There is nothing to defer to.

### 12.1 `Std.Time`

The value types are immutable classes with `compare_to`, `equals`, `hash_code`, and `to_string`. Their arithmetic is checked integer arithmetic and their text is digits and punctuation joined by concatenation. Each `parse` needs T1.

**`Duration`** is a signed count of nanoseconds in one `int64` (about ±292 years).

- Module functions: `of_nanos`, `of_micros`, `of_millis`, `of_seconds`, `of_minutes`, `of_hours`, `of_days` (24 hours), each taking `int64`; the constant `Zero`; `parse(text) → Result<Duration>`. Overflow traps (reason 1), here and in every member below.
- Members: `plus`, `minus`, `multiplied_by(int64)`, `divided_by(int64)` (truncating), `negated`, `abs`, `is_zero`, `is_negative`, `to_nanos`, `to_micros`, `to_millis`, `to_seconds` (each truncating toward zero), and `to_seconds_float64`.
- `to_string` is ISO 8601: `PT`, then hours, minutes, and seconds with their unit letters, omitting zero units, with a fractional second trimmed of trailing zeros (`PT1H30M`, `PT0.5S`). Zero is `PT0S`. A negative duration is `-` followed by the text of its absolute value.

**`Instant`** is a point on the UTC time line: `int64` seconds since 1970-01-01T00:00:00Z and an `int` nanosecond adjustment from 0 through 999,999,999. Leap seconds are ignored, as in POSIX time.

- Module functions `of_epoch_seconds(seconds, nanos)`, `of_epoch_millis`, the constant `Epoch`, and `parse`. Members `plus(Duration)`, `minus(Duration)`, `duration_since(other) → Duration`, `epoch_seconds()`, `nanos()`, `to_epoch_millis()`, `to_local(offset: ZoneOffset) → LocalDateTime`.
- `to_string` is `YYYY-MM-DDTHH:MM:SS` with a fraction of 3, 6, or 9 digits when the nanoseconds are not zero (the shortest of those that is exact), then `Z`. `parse` accepts that form and a numeric offset in place of `Z`.

**`Clock`** is an interface with `now() → Result<Instant>`, `monotonic() → Result<Duration>` (elapsed time from an arbitrary fixed origin, never decreasing), and `sleep(duration) → Result<Unit>`.

- `SystemClock` uses the clock host services (P2) and fails with `CapabilityDenied` when they are not allowed.
- `FixedClock` holds an instant set at construction; `set(instant)` and `advance(duration)` change it; `sleep` advances it and returns at once; it never fails. Library code that needs time takes a `Clock`, so tests pass a `FixedClock`.
- `Stopwatch` takes a `Clock` and offers `start`, `stop`, `reset`, and `elapsed() → Result<Duration>`.

**Calendar types** use the proleptic Gregorian calendar.

- `LocalDate` stores `year` (-999,999 through 999,999), `month` (1 through 12), and `day`. The module function `of(year, month, day)` traps for an invalid date. `try_of(year, month, day) → LocalDate` returns `nil` for invalid components. Conversion to and from a day count since 1970-01-01 (`to_epoch_day`, `of_epoch_day`) uses the 400-year cycle of 146,097 days. Shift the year so that it starts in March, split it into the cycle and the year within the cycle, and compute the day of the cycle using floor division so that negative years work. Members include `plus_days`, `plus_months`, `plus_years`, `days_until(other) → int64`, `day_of_week() → DayOfWeek`, `day_of_year()`, `is_leap_year()`, and `length_of_month()`. Month and year changes clamp the day to the end of the target month. `DayOfWeek` runs from `Monday` through `Sunday`, with epoch day 0 on Thursday. Text uses `YYYY-MM-DD`. Years outside 0000 through 9999 have a sign and at least four digits.
- `LocalTime`: `hour`, `minute`, `second`, `nano`, stored as nanoseconds of the day. `plus(Duration)` wraps around midnight. Text form `HH:MM:SS` with the same fraction rule as `Instant`.
- `LocalDateTime`: a `LocalDate` and a `LocalTime`. `plus(Duration)` carries into the date. `at_offset(offset) → Instant`. Text form: the date, `T`, the time.
- `ZoneOffset`: total seconds from -64,800 through 64,800. The constant `Utc`, `of_hours_minutes`, text form `Z` or `±HH:MM`.

There is no time zone database, no named zones, and no daylight-saving rules. Conversions between instants and local times take an explicit `ZoneOffset`. The ISO forms above are the only text forms.

### 18.5 Time, randomness, and logging

- **Duration text.** `parse` accepts an optional sign, `P`, an optional day count, and `T` with hours, minutes, and seconds (seconds with up to nine fraction digits); days are read but never written.
- **Instant** normalizes the nanoseconds of `of_epoch_seconds` by floor division and writes every `int64` second count; `parse` accepts years -999,999 through 999,999.
- **ZoneOffset** holds whole minutes; hours and minutes share a sign, and `compare_to` orders by total seconds.
- **Clocks.** `Clock.sleep` returns at once for a zero or negative duration. `FixedClock.of(instant)` makes a fixed clock whose monotonic reading is the total of its advances and sleeps; `advance` traps for a negative duration. `Stopwatch.create(clock)`; `start` and `stop` return `Result<Unit>` and trap when repeated.
- **Calendar arithmetic** splits off 400-year cycles with one 64-bit division and works within a cycle in 32-bit arithmetic, which keeps images that format dates several megabytes smaller.
- **Random.** `seeded` and `from_entropy` are the only constructors; `from_entropy` fails with `InvalidData` when the host returns other than 8 bytes. **Uuid** stores two big-endian `uint64` halves and has `to_bytes`.
- **Trap details.** `invalid date`, `date out of range`, `invalid time`, `invalid zone offset`, `day of week`, `negative duration`, `bound must be positive`, `low must be below high`, and `negative count` after `invalid argument: `; `stopwatch is running` and `stopwatch is stopped` after `invalid state: `.
- **Logger.** `is_enabled(Off)` is false.

### Task safety

A function that a spawned task calls must be task-safe ([Concurrency, section 5.2](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#52-task-safe-calls)). Every function of this module is. The constant `Epoch` is an object, and a constant that is an object is module state a spawned task may not read; a task writes `of_epoch_seconds(0, 0)`. [Task safety of the library](StandardLibrary#task-safety) lists every exception, and `neura check --task-safety` reports the answer for any function.

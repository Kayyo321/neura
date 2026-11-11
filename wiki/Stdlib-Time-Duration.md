# `Std.Time.Duration`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Time.Duration`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Time.Duration`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Duration** · [`export let Duration = class implements Comparable<Duration>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L13)
  An immutable signed duration represented as an exact count of nanoseconds.
- **Duration.Duration** · [`export let init = func(nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L18)
  Stores the signed nanosecond count.
- **Duration.Duration** · [`export let plus = func(other: Duration) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L23)
  Adds two durations with checked nanosecond arithmetic.
- **Duration.Duration** · [`export let minus = func(other: Duration) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L26)
  Subtracts another duration with checked nanosecond arithmetic.
- **Duration.Duration** · [`export let multiplied_by = func(factor: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L29)
  Scales the duration by an integer. Overflow traps.
- **Duration.Duration** · [`export let divided_by = func(divisor: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L33)
  Truncates toward zero. A zero divisor traps (reason 1), as does the most negative duration divided by -1.
- **Duration.Duration** · [`export let negated = func -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L36)
  Reverses the sign. The most negative duration cannot be negated.
- **Duration.Duration** · [`export let abs = func -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L39)
  Returns the nonnegative magnitude. The most negative duration traps.
- **Duration.Duration** · [`export let is_zero = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L47)
  Reports whether the nanosecond count is zero.
- **Duration.Duration** · [`export let is_negative = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L50)
  Reports whether the nanosecond count is below zero.
- **Duration.Duration** · [`export let to_nanos = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L53)
  Returns the exact signed nanosecond count.
- **Duration.Duration** · [`export let to_micros = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L56)
  The to_ conversions truncate toward zero.
- **Duration.Duration** · [`export let to_millis = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L59)
  Returns whole milliseconds, truncating toward zero.
- **Duration.Duration** · [`export let to_seconds = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L62)
  Returns whole seconds, truncating toward zero.
- **Duration.Duration** · [`export let to_seconds_float64 = func -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L66)
  The nanoseconds converted to float64, divided by 10^9. Exact conversion, so correctly rounded, while the count is at most 2^53.
- **Duration.Duration** · [`export let compare_to = func(other: Duration) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L69)
  Orders durations by their signed nanosecond counts.
- **Duration.Duration** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L80)
  Tests equality of signed nanosecond counts.
- **Duration.Duration** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L85)
  Hashes the signed nanosecond count.
- **Duration.Duration** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L91)
  ISO 8601: PT, then hours, minutes, and seconds with their unit letters, zero units omitted, the fraction trimmed of trailing zeros. Zero is PT0S; a negative duration is - and the text of its magnitude.
- **Duration** · [`export const Zero: Duration = new Duration(0)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L117)
  The duration of zero nanoseconds.
- **Duration** · [`export let of_nanos = func(nanos: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L120)
  Constructs a duration from an exact nanosecond count.
- **Duration** · [`export let of_micros = func(micros: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L123)
  Converts microseconds to nanoseconds. Overflow traps.
- **Duration** · [`export let of_millis = func(millis: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L126)
  Converts milliseconds to nanoseconds. Overflow traps.
- **Duration** · [`export let of_seconds = func(seconds: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L129)
  Converts seconds to nanoseconds. Overflow traps.
- **Duration** · [`export let of_minutes = func(minutes: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L132)
  Converts 60-second minutes to nanoseconds. Overflow traps.
- **Duration** · [`export let of_hours = func(hours: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L135)
  Converts 60-minute hours to nanoseconds. Overflow traps.
- **Duration** · [`export let of_days = func(days: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L138)
  A day is 24 hours.
- **Duration** · [`export let parse = func(text: string) -> result.Result<Duration>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L146)
  An optional sign, P, an optional day count with D, then optionally T and at least one of hours (H), minutes (M), and seconds (S) in that order; seconds may have a fraction of one through nine digits. At least one component is required, and letters are uppercase. The to_string form always parses. Errors: Empty; InvalidCharacter or UnexpectedEnd at the first byte that does not fit; OutOfRange at the first digit of the component that leaves int64.
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

### 11.4 `Std.Net`

TCP only. A call waits for its result; in a program with tasks the waiting task parks and the others run, and cancelling its scope ends the wait with kind `Cancelled` ([Concurrency](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#104-host-io)). Needs P2 and T1.

- `TcpStream`: the module functions `connect(host: string, port: int, timeout: Duration) → Result<TcpStream>` and `with_connection<T>(host, port, body)`, the scoped helper of [section 5.5](Stdlib-Design#55-resources). Members `read` (a `ByteSource` when bound), `write` (a `ByteSink` when bound), `set_read_timeout(timeout: Duration)` (zero for none; an expired timeout fails with kind `TimedOut`), and `close()`. A write that is cancelled while it waits fails with kind `Cancelled` and the message `stream write cancelled after N bytes`, where N is the number of bytes already sent.
- `TcpListener`: the module function `bind(host: string, port: int) → Result<TcpListener>`. Members `accept() → Result<TcpStream>`, `local_port() → int`, and `close()`. Port 0 asks the host for a free port.
- A port outside 0 through 65,535 traps.
- Service arguments: `connect` takes the host text, the port, and the timeout in nanoseconds; `listen` takes the host text and port; `accept` takes a listener ID; `read` takes a socket ID, a maximum count, and a timeout in nanoseconds (0 for none); `write` takes a socket ID and bytes; `close` takes either kind of ID.

UDP, TLS, HTTP, and name-resolution APIs are not in the first version.

### 12.1 `Std.Time`

The value types are immutable classes with `compare_to`, `equals`, `hash_code`, and `to_string`. They can be written today: their arithmetic is checked integer arithmetic and their text is digits and punctuation joined by concatenation. Each `parse` needs T1.

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

**Decision:** there is no time zone database, no named zones, and no daylight-saving rules in the first version. Conversions between instants and local times take an explicit `ZoneOffset`. The ISO forms above are the only text forms.

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

A function that a spawned task calls must be task-safe ([Concurrency, section 5.2](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#52-task-safe-calls)). Every function of this module is. The constant `Zero` is an object, and a constant that is an object is module state a spawned task may not read; a task writes `of_nanos(0)`. [Task safety of the library](StandardLibrary#task-safety) lists every exception, and `neura check --task-safety` reports the answer for any function.

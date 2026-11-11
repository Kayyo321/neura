# `Std.Time.ZoneOffset`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Time.ZoneOffset`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/zone-offset.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Time.ZoneOffset`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ZoneOffset** · [`export let ZoneOffset = class implements Comparable<ZoneOffset>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/zone-offset.nra#L14)
  An immutable fixed offset from UTC with whole-minute resolution. Tagged `#Final`, `#NoDiscard`, `#Immutable`, `#ThreadSafe`.
- **ZoneOffset.ZoneOffset** · [`export last total_seconds: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/zone-offset.nra#L16)
  The signed offset in seconds east of UTC: -64,800 through 64,800, a multiple of 60.
- **ZoneOffset.ZoneOffset** · [`export let init = func(total: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/zone-offset.nra#L19)
  Stores the supplied offset in seconds.
- **ZoneOffset.ZoneOffset** · [`export let compare_to = func(other: ZoneOffset) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/zone-offset.nra#L25)
  By total seconds, west before east. Tagged `#Pure`.
- **ZoneOffset.ZoneOffset** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/zone-offset.nra#L37)
  Tests equality of offsets in seconds. Tagged `#Pure`.
- **ZoneOffset.ZoneOffset** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/zone-offset.nra#L43)
  Hashes the offset in seconds. Tagged `#Pure`.
- **ZoneOffset.ZoneOffset** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/zone-offset.nra#L47)
  Z for zero; otherwise +HH:MM or -HH:MM. Tagged `#Pure`.
- **ZoneOffset** · [`export const Utc: ZoneOffset = new ZoneOffset(0)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/zone-offset.nra#L51)
  The zero offset from UTC.
- **ZoneOffset** · [`export let of_hours_minutes = func(hours: int, minutes: int) -> ZoneOffset`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/zone-offset.nra#L56)
  Hours -18 through 18 and minutes -59 through 59 with the same sign (either may be zero), at most 18:00 from zero. Anything else traps with "invalid argument: invalid zone offset". Tagged `#Pure`, `#ThreadSafe`.
- **ZoneOffset** · [`export let parse = func(text: string) -> result.Result<ZoneOffset>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/zone-offset.nra#L73)
  Z, or a sign and HH:MM no further than 18:00 from zero. -00:00 and +00:00 are Utc. Errors: Empty; InvalidCharacter or UnexpectedEnd at the first byte that does not fit; OutOfRange at an hour above 18 or a minute above 59, or at the sign when the total exceeds 18:00. Tagged `#NoDiscard`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 12.1 `Std.Time`

The value types are immutable classes with `compare_to`, `equals`, `hash_code`, and `to_string`. Their arithmetic is checked integer arithmetic and their text is digits and punctuation joined by concatenation. Each `parse` needs T1.

**`Duration`** is a signed count of nanoseconds in one `int64` (about ±292 years).

- Module functions: `of_nanos`, `of_micros`, `of_millis`, `of_seconds`, `of_minutes`, `of_hours`, `of_days` (24 hours), each taking `int64`; the constant `Zero`; `parse(text) → Result<Duration>`. Overflow traps (reason 1), here and in every member below.
- Members: `plus`, `minus`, `multiplied_by(int64)`, `divided_by(int64)` (truncating), `negated`, `abs`, `is_zero`, `is_negative`, `to_nanos`, `to_micros`, `to_millis`, `to_seconds` (each truncating toward zero), and `to_seconds_float64`.
- `to_string` is ISO 8601: `PT`, then hours, minutes, and seconds with their unit letters, omitting zero units, with a fractional second trimmed of trailing zeros (`PT1H30M`, `PT0.5S`). Zero is `PT0S`. A negative duration is `-` followed by the text of its absolute value.

**`Instant`** is a point on the UTC time line: `int64` seconds since 1970-01-01T00:00:00Z and an `int` nanosecond adjustment from 0 through 999,999,999. Leap seconds are ignored, as in POSIX time.

- Module functions `of_epoch_seconds(seconds, nanos)`, `of_epoch_millis`, the constant `Epoch`, and `parse`. The read-only fields `epoch_seconds` and `nanos`, and the members `plus(Duration)`, `minus(Duration)`, `duration_since(other) → Duration`, `to_epoch_millis()`, `to_local(offset: ZoneOffset) → LocalDateTime`.
- `to_string` is `YYYY-MM-DDTHH:MM:SS` with a fraction of 3, 6, or 9 digits when the nanoseconds are not zero (the shortest of those that is exact), then `Z`. `parse` accepts that form and a numeric offset in place of `Z`.

**`Clock`** is an interface with `now() → Result<Instant>`, `monotonic() → Result<Duration>` (elapsed time from an arbitrary fixed origin, never decreasing), and `sleep(duration) → Result<Unit>`.

- `SystemClock` uses the clock host services (P2) and fails with `CapabilityDenied` when they are not allowed.
- `FixedClock` holds an instant set at construction; `set(instant)` and `advance(duration)` change it; `sleep` advances it and returns at once; it never fails. Library code that needs time takes a `Clock`, so tests pass a `FixedClock`.
- `Stopwatch` takes a `Clock` and offers `start`, `stop`, `reset`, `elapsed() → Result<Duration>`, and the read-only field `is_running`.

**Calendar types** use the proleptic Gregorian calendar.

- `LocalDate` stores `year` (-999,999 through 999,999), `month` (1 through 12), and `day`, read-only fields. The module function `of(year, month, day)` traps for an invalid date. `try_of(year, month, day) → LocalDate` returns `nil` for invalid components. Conversion to and from a day count since 1970-01-01 (`to_epoch_day`, `of_epoch_day`) uses the 400-year cycle of 146,097 days. Shift the year so that it starts in March, split it into the cycle and the year within the cycle, and compute the day of the cycle using floor division so that negative years work. Members include `plus_days`, `plus_months`, `plus_years`, `days_until(other) → int64`, `day_of_week() → DayOfWeek`, `day_of_year()`, `is_leap_year()`, and `length_of_month()`. Month and year changes clamp the day to the end of the target month. `DayOfWeek` runs from `Monday` through `Sunday`, with epoch day 0 on Thursday. Text uses `YYYY-MM-DD`. Years outside 0000 through 9999 have a sign and at least four digits.
- `LocalTime`: `hour()`, `minute()`, `second()`, `nano()`, computed from the nanoseconds of the day that it stores (`to_nano_of_day()`). `plus(Duration)` wraps around midnight. Text form `HH:MM:SS` with the same fraction rule as `Instant`.
- `LocalDateTime`: a `LocalDate` and a `LocalTime`, the read-only fields `date` and `time`. `plus(Duration)` carries into the date. `at_offset(offset) → Instant`. Text form: the date, `T`, the time.
- `ZoneOffset`: total seconds from -64,800 through 64,800, the read-only field `total_seconds`. The constant `Utc`, `of_hours_minutes`, text form `Z` or `±HH:MM`.

A read-only field is an exported `last` field (an `is_running` with a get-only [accessor](Types#accessors)): code outside the class reads it as `date.year` and cannot assign it. The value classes are `#Final`, and `#NoDiscard`, so a call whose result is dropped, such as `instant.plus(span);`, warns ([Tags](Tags)).

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

A function that a spawned task calls must be task-safe ([Concurrency, section 5.2](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#52-task-safe-calls)). Every function of this module is. The constant `Utc` is an object, and a constant that is an object is module state a spawned task may not read; a task writes `of_hours_minutes(0, 0)`. [Task safety of the library](StandardLibrary#task-safety) lists every exception, and `neura check --task-safety` reports the answer for any function.

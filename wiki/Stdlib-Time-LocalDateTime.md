# `Std.Time.LocalDateTime`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Time.LocalDateTime`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date-time.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Time.LocalDateTime`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **LocalDateTime** · [`export let LocalDateTime = class implements Comparable<LocalDateTime>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date-time.nra#L14)
- **LocalDateTime.LocalDateTime** · [`export let init = func(on: local_date.LocalDate, at: local_time.LocalTime)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date-time.nra#L18)
- **LocalDateTime.LocalDateTime** · [`export let date = func -> local_date.LocalDate`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date-time.nra#L23)
- **LocalDateTime.LocalDateTime** · [`export let time = func -> local_time.LocalTime`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date-time.nra#L25)
- **LocalDateTime.LocalDateTime** · [`export let plus = func(span: duration.Duration) -> LocalDateTime`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date-time.nra#L29)
  Carries whole days into the date. Traps with "invalid argument: date out of range" when the date leaves its range.
- **LocalDateTime.LocalDateTime** · [`export let at_offset = func(offset: zone_offset.ZoneOffset) -> instant.Instant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date-time.nra#L42)
  The instant at which UTC reads this date and time minus the offset.
- **LocalDateTime.LocalDateTime** · [`export let compare_to = func(other: LocalDateTime) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date-time.nra#L49)
- **LocalDateTime.LocalDateTime** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date-time.nra#L57)
- **LocalDateTime.LocalDateTime** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date-time.nra#L65)
- **LocalDateTime.LocalDateTime** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date-time.nra#L70)
  The date, T, and the time.
- **LocalDateTime** · [`export let of = func(on: local_date.LocalDate, at: local_time.LocalTime) -> LocalDateTime`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date-time.nra#L73)
- **LocalDateTime** · [`export let parse = func(text: string) -> result.Result<LocalDateTime>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date-time.nra#L78)
  The LocalDate form, T, and the LocalTime form, with their errors.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

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

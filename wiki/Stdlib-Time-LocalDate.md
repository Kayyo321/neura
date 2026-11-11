# `Std.Time.LocalDate`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Time.LocalDate`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Time.LocalDate`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **LocalDate** · [`export let LocalDate = class implements Comparable<LocalDate>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L15)
  An immutable Gregorian date within the supported year range. Tagged `#Final`, `#NoDiscard`, `#Immutable`, `#ThreadSafe`.
- **LocalDate.LocalDate** · [`export last year: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L17)
  The Gregorian year, using astronomical year numbering: -999,999 through 999,999.
- **LocalDate.LocalDate** · [`export last month: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L19)
  The month of the year, 1 through 12.
- **LocalDate.LocalDate** · [`export last day: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L21)
  The day of the month, 1 through the length of the month.
- **LocalDate.LocalDate** · [`export let init = func(year: int, month: int, day: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L24)
  Stores the supplied Gregorian date components.
- **LocalDate.LocalDate** · [`export let to_epoch_day = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L32)
  Returns the signed number of days since 1970-01-01. Tagged `#Pure`.
- **LocalDate.LocalDate** · [`export let plus_days = func(days: int64) -> LocalDate`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L39)
  These trap with "invalid argument: date out of range" when the result's year is outside -999,999 through 999,999. Tagged `#Pure`.
- **LocalDate.LocalDate** · [`export let plus_months = func(months: int64) -> LocalDate`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L43)
  Month and year changes clamp the day to the end of the target month. Tagged `#Pure`.
- **LocalDate.LocalDate** · [`export let plus_years = func(years: int64) -> LocalDate`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L52)
  Adds whole calendar years and clamps the day to the last day of the target month when necessary. Tagged `#Pure`.
- **LocalDate.LocalDate** · [`export let days_until = func(other: LocalDate) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L58)
  other's epoch day minus this one's. Tagged `#Pure`.
- **LocalDate.LocalDate** · [`export let day_of_week = func -> weekdays.DayOfWeek`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L62)
  Returns the weekday of this date in ISO order. Tagged `#Pure`.
- **LocalDate.LocalDate** · [`export let day_of_year = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L69)
  1 through 366. Tagged `#Pure`.
- **LocalDate.LocalDate** · [`export let is_leap_year = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L76)
  Reports whether this date falls in a Gregorian leap year. Tagged `#Pure`.
- **LocalDate.LocalDate** · [`export let length_of_month = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L80)
  Returns the number of days in this date's month. Tagged `#Pure`.
- **LocalDate.LocalDate** · [`export let compare_to = func(other: LocalDate) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L84)
  Orders dates by year, month, and then day. Tagged `#Pure`.
- **LocalDate.LocalDate** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L99)
  Tests whether the year, month, and day all agree. Tagged `#Pure`.
- **LocalDate.LocalDate** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L109)
  Combines the hashes of the year, month, and day. Tagged `#Pure`.
- **LocalDate.LocalDate** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L117)
  YYYY-MM-DD; a year outside 0000 through 9999 has a sign and at least four digits. Tagged `#Pure`.
- **LocalDate** · [`export let of = func(year: int, month: int, day: int) -> LocalDate`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L125)
  Traps with "invalid argument: invalid date" for a year outside -999,999 through 999,999, a month outside 1 through 12, or a day outside the month. Tagged `#Pure`, `#ThreadSafe`.
- **LocalDate** · [`export let try_of = func(year: int, month: int, day: int) -> LocalDate`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L134)
  nil for invalid components. Tagged `#Pure`, `#ThreadSafe`.
- **LocalDate** · [`export let of_epoch_day = func(epoch_day: int64) -> LocalDate`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L143)
  Traps with "invalid argument: date out of range" outside -999999-01-01 through 999999-12-31. Tagged `#Pure`, `#ThreadSafe`.
- **LocalDate** · [`export let parse = func(text: string) -> result.Result<LocalDate>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/local-date.nra#L156)
  YYYY-MM-DD, where the year is four digits or a sign and at least four digits. Errors: Empty; InvalidCharacter or UnexpectedEnd at the first byte that does not fit; OutOfRange at the first byte of a year beyond 999,999, a month outside 1 through 12, or a day outside the month. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
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

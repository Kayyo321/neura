# `Std.Net.Http.Date`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.Date`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/date.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.Date`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Date** · [`export const MinEpochSeconds: int64 = -62167219200`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/date.nra#L24)
  The first second that IMF-fixdate can write: 0000-01-01T00:00:00Z.
- **Date** · [`export const MaxEpochSeconds: int64 = 253402300799`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/date.nra#L27)
  The last second that IMF-fixdate can write: 9999-12-31T23:59:59Z.
- **Date** · [`export let format = func(moment: instant.Instant) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/date.nra#L32)
  `moment` as IMF-fixdate, for example `Sun, 06 Nov 1994 08:49:37 GMT`. A fraction of a second is dropped (the time is rounded toward the past). Err (OutOfRange) outside years 0000 through 9999. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Date** · [`export let parse = func(text: string, reference: instant.Instant) -> Result<instant.Instant>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/date.nra#L54)
  Parses an HTTP-date in any of the three formats, matching names exactly. `reference` (normally the time the message was received) resolves the two-digit year of the RFC 850 form. Errors are Parse errors at the offending byte: Empty for "", Syntax for text that fits no format, InvalidCharacter for a bad name, digit or separator, OutOfRange for an impossible date or time or a day name that is not the date's weekday, UnexpectedEnd for a cut text. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Date** · [`export let parse_ignoring_case = func(text: string, reference: instant.Instant) -> Result<instant.Instant>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/date.nra#L60)
  `parse`, but day names, month names and `GMT` match without regard to ASCII case (RFC 9111 section 4.2). Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Date** · [`export let parse_imf_fixdate = func(text: string) -> Result<instant.Instant>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/date.nra#L65)
  Parses only the preferred IMF-fixdate format, matching names exactly; no reference is needed. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.

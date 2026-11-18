# `Std.Net.Dns.DnsCache`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.DnsCache`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.DnsCache`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **DnsCache** · [`export const DefaultCapacity: int = 1024`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L27)
  The default capacity.
- **DnsCache** · [`export const DefaultMinTtl: int64 = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L29)
  The default lower TTL clamp in seconds.
- **DnsCache** · [`export const DefaultMaxTtl: int64 = 86400`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L31)
  The default upper TTL clamp in seconds (one day).
- **DnsCache** · [`export const DefaultMaxNegativeTtl: int64 = 10800`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L33)
  The default cap on negative TTLs in seconds (three hours, the upper end RFC 2308 section 5 suggests).
- **DnsCache** · [`export let CacheResult = enum ( Miss, Answer(messages.ResourceRecord[list]), NameError, NoData )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L36)
  What a lookup found.
- **DnsCache** · [`export let DnsCache = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L66)
  The cache. Tagged `#Final`, `#NotThreadSafe`.
- **DnsCache.DnsCache** · [`export last capacity: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L70)
  The most entries kept.
- **DnsCache.DnsCache** · [`export last min_ttl: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L72)
  The lower TTL clamp in seconds.
- **DnsCache.DnsCache** · [`export last max_ttl: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L74)
  The upper TTL clamp in seconds.
- **DnsCache.DnsCache** · [`export last max_negative_ttl: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L76)
  The cap on negative TTLs in seconds.
- **DnsCache.DnsCache** · [`export let init = func(time: Clock)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L90)
  A cache over `time` with the defaults; or with a capacity and TTL clamps (seconds). A capacity below 1, a negative clamp or `min` above `max` traps with "invalid argument: cache".
- **DnsCache.DnsCache** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L109)
  The entries held, expired ones included until they are dropped. Tagged `#NoDiscard`.
- **DnsCache.DnsCache** · [`export let hits = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L113)
  Lookups that found an entry. Tagged `#NoDiscard`.
- **DnsCache.DnsCache** · [`export let misses = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L117)
  Lookups that found none. Tagged `#NoDiscard`.
- **DnsCache.DnsCache** · [`export let evictions = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L121)
  Entries dropped to make room. Tagged `#NoDiscard`.
- **DnsCache.DnsCache** · [`export let get = func(question: messages.Question) -> CacheResult`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L126)
  The cached result for a question: a name error for its name and class, otherwise the answer or no-data entry for its name, type and class; Miss when none is live. Tagged `#NoDiscard`.
- **DnsCache.DnsCache** · [`export let put_answer = func(question: messages.Question, records: messages.ResourceRecord[list]) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L161)
  Caches `records` as the answer to `question` for their smallest TTL, clamped. An empty list stores nothing. Returns the TTL used in seconds, or -1 when nothing was stored.
- **DnsCache.DnsCache** · [`export let put_negative = func(question: messages.Question, name_error: bool, authority: messages.ResourceRecord[list]) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L178)
  Caches a negative answer from the authority section of a response (RFC 2308 section 5): `name_error` true for NXDOMAIN, false for NODATA. Returns the TTL used, or -1 when the authority section has no SOA and nothing was stored.
- **DnsCache.DnsCache** · [`export let remove = func(question: messages.Question)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L192)
  Drops every entry for the question's name, type and class, and its name error.
- **DnsCache.DnsCache** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L198)
  Drops everything.
- **DnsCache.DnsCache** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L204)
  Text such as "DnsCache(3 of 1024 entries)".
- **DnsCache** · [`export let negative_ttl = func(authority: messages.ResourceRecord[list]) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-cache.nra#L298)
  The negative TTL of an authority section: the smaller of the first SOA record's TTL and its MINIMUM (RFC 2308 sections 3 and 5), or -1 when there is no SOA. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.

# `Std.Net.Http.Cache`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.Cache`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.Cache`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Cache** · [`export const Infinity: int64 = 2147483648`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L31)
  The saturation bound of age and lifetime arithmetic, in seconds (RFC 9111 section 1.2.2).
- **Cache** · [`export const HeuristicPercent: int64 = 10`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L34)
  The percentage of the time since Last-Modified used as a heuristic lifetime (section 4.2.2).
- **Cache** · [`export let StoredResponse = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L39)
  A response as a cache keeps it: status, header fields, content, and the times of the request and of the response's arrival (section 4.2.3). Tagged `#Final`, `#NotThreadSafe`.
- **Cache.StoredResponse** · [`export last status: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L41)
  The status code.
- **Cache.StoredResponse** · [`export last fields: header_map.Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L43)
  The response header fields (a copy owned by the stored response).
- **Cache.StoredResponse** · [`export last body: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L45)
  The response content (a copy owned by the stored response).
- **Cache.StoredResponse** · [`export last request_time: instant.Instant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L47)
  The clock reading when the request was sent ("request_time").
- **Cache.StoredResponse** · [`export last response_time: instant.Instant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L49)
  The clock reading when the response was received ("response_time").
- **Cache.StoredResponse** · [`export let init = func(code: int, response_fields: header_map.Headers, content: uint8[list], requested: instant.Instant, received: instant.Instant)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L52)
  Copies the header fields and the content.
- **Cache.StoredResponse** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L65)
  Equal status, fields, content and times.
- **Cache.StoredResponse** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L83)
  Combines the status, fields, content and times.
- **Cache.StoredResponse** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L94)
  The status, the number of fields and content bytes, and the two times.
- **Cache** · [`export let AgeTerms = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L102)
  Every term of the age calculation of RFC 9111 section 4.2.3, in seconds. Tagged `#Final`, `#Immutable`, `#ThreadSafe`.
- **Cache.AgeTerms** · [`export last age_value: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L104)
  The Age field value, or 0 when absent or invalid.
- **Cache.AgeTerms** · [`export last date_value: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L106)
  The Date field value as epoch seconds (the response time when absent or invalid).
- **Cache.AgeTerms** · [`export last apparent_age: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L108)
  max(0, response_time - date_value).
- **Cache.AgeTerms** · [`export last response_delay: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L110)
  response_time - request_time.
- **Cache.AgeTerms** · [`export last corrected_age_value: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L112)
  age_value + response_delay.
- **Cache.AgeTerms** · [`export last corrected_initial_age: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L114)
  max(apparent_age, corrected_age_value).
- **Cache.AgeTerms** · [`export last resident_time: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L116)
  now - response_time.
- **Cache.AgeTerms** · [`export last current_age: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L118)
  corrected_initial_age + resident_time.
- **Cache.AgeTerms** · [`export let init = func(age: int64, date: int64, apparent: int64, delay: int64, corrected: int64, initial: int64, resident: int64, current: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L121)
  Stores the eight terms.
- **Cache.AgeTerms** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L134)
  Equal terms.
- **Cache.AgeTerms** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L142)
  The hash of the text form.
- **Cache.AgeTerms** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L145)
  The terms by name.
- **Cache** · [`export let Lifetime = enum (SharedMaxAge(int64), MaxAge(int64), Expires(int64), Heuristic(int64), Unknown)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L154)
  Which rule of RFC 9111 section 4.2.1 gave the freshness lifetime, with the lifetime in seconds.
- **Cache** · [`export let Lookup = enum (Fresh(StoredResponse), Stale(StoredResponse), Miss)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L162)
  The result of a cache lookup. `Fresh`: the stored response may be used without contacting the origin (it is fresh and meets the request's max-age and min-fresh, or it is stale within the request's max-stale where the response allows that). `Stale`: a stored response exists but must be validated (section 4.3) before use. `Miss`: nothing usable is stored. The payload is the cache's own stored response: treat it as read-only and copy its fields (`fields.copy()`) before changing them, for example to replace `Age` (section 4), or the stored entry changes too.
- **Cache** · [`export let age_terms = func(stored: StoredResponse, now: instant.Instant) -> AgeTerms`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L166)
  The age terms of section 4.2.3 at `now`. Tagged `#NoDiscard`.
- **Cache** · [`export let current_age = func(stored: StoredResponse, now: instant.Instant) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L181)
  current_age of section 4.2.3 in seconds.
- **Cache** · [`export let lifetime = func(stored: StoredResponse, shared: bool, heuristic: bool) -> Lifetime`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L189)
  The freshness lifetime by the first matching rule of section 4.2.1: s-maxage (shared caches only), max-age, Expires minus Date, then, when `heuristic` is true and the status is heuristically cacheable or the response is `public`, 10% of Date minus Last-Modified (section 4.2.2; never negative). `Unknown` when no rule applies. Tagged `#NoDiscard`.
- **Cache** · [`export let lifetime_seconds = func(value: Lifetime) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L232)
  The seconds of a lifetime; 0 for `Unknown`. Tagged `#Pure`, `#ThreadSafe`.
- **Cache** · [`export let freshness_lifetime = func(stored: StoredResponse, shared: bool, heuristic: bool) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L241)
  freshness_lifetime of section 4.2.1 in seconds (0 when no rule applies); see `lifetime`.
- **Cache** · [`export let is_fresh = func(stored: StoredResponse, now: instant.Instant, shared: bool, heuristic: bool) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L246)
  response_is_fresh = (freshness_lifetime > current_age) (section 4.2). A negative current_age (`now` before the response time) counts as 0, so a lifetime of 0 or less is never fresh.
- **Cache** · [`export let is_storable = func(method: string, request_fields: header_map.Headers, status: int, response_fields: header_map.Headers, shared: bool) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L251)
  Whether a cache may store the response (section 3, with section 3.5 and the request no-store directive of section 5.2.1.5); see `refusal`.
- **Cache** · [`export let refusal = func(method: string, request_fields: header_map.Headers, status: int, response_fields: header_map.Headers, shared: bool) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L263)
  Why a cache must not store the response, or "" when it may. The reasons, checked in this order: "method" (only GET and HEAD are understood), "status" (not final), "status not understood" (206 or 304, or any status that is not final under must-understand), "request no-store", "no-store" (ignored with must-understand, section 5.2.2.3), "private" (shared caches), "authorization" (shared caches, a request with Authorization and no must-revalidate, public or s-maxage, section 3.5), "vary *", and "no freshness information" (none of public, private in a private cache, Expires, max-age, s-maxage in a shared cache, or a heuristically cacheable status). Tagged `#NoDiscard`.
- **Cache** · [`export let ResponseCache = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L372)
  An in-memory HTTP cache keyed by request method and target URI text, holding responses to GET and HEAD only, at most `capacity` of them. Storing a new key into a full cache evicts the least recently used entry (by a counter that each store and each Fresh or Stale lookup advances, so eviction is deterministic); storing a key that is already present replaces its response in place and evicts nothing. A shared cache applies the shared-cache rules (s-maxage, private, proxy-revalidate, Authorization). Heuristic freshness is on by default. The cache does not validate, collapse requests or serve `only-if-cached` itself: a Miss for an only-if-cached request is the caller's 504 (section 5.2.1.7). Tagged `#Final`, `#NotThreadSafe`.
- **Cache.ResponseCache** · [`export last shared: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L374)
  Whether the cache is shared (a proxy) rather than private (one user agent).
- **Cache.ResponseCache** · [`export last capacity: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L376)
  The maximum number of stored responses.
- **Cache.ResponseCache** · [`export let init = func(is_shared: bool, max_entries: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L385)
  An empty cache. Traps with "invalid argument: capacity" when `max_entries` is below 1.
- **Cache.ResponseCache** · [`export let set_heuristic = func(enabled: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L394)
  Turns heuristic freshness on or off.
- **Cache.ResponseCache** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L399)
  The number of stored responses.
- **Cache.ResponseCache** · [`export let contains = func(method: string, uri: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L402)
  Whether a response is stored for `method` and `uri`.
- **Cache.ResponseCache** · [`export let store = func(method: string, uri: string, request_fields: header_map.Headers, response: StoredResponse) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L406)
  Stores the response to a `method` request for `uri` when section 3 allows it (see `refusal`); returns whether it was stored.
- **Cache.ResponseCache** · [`export let lookup = func(method: string, uri: string, request_fields: header_map.Headers, now: instant.Instant) -> Lookup`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L429)
  Looks up a stored response for a `method` request for `uri` at `now`, applying the request's no-cache, max-age, min-fresh and max-stale directives (section 5.2.1) and the response's no-cache, must-revalidate, proxy-revalidate and s-maxage limits on serving stale responses (section 4.2.4). Tagged `#NoDiscard`.
- **Cache.ResponseCache** · [`export let lookup_with = func(clock: Clock, method: string, uri: string, request_fields: header_map.Headers) -> Result<Lookup>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L475)
  `lookup` at the clock's current time; the clock's error, if any, is returned. Tagged `#NoDiscard`.
- **Cache.ResponseCache** · [`export let invalidate = func(uri: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L482)
  Removes every stored response for `uri` (any method); returns how many were removed.
- **Cache.ResponseCache** · [`export let record_response = func(method: string, uri: string, status: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L497)
  Section 4.4: after a non-error (2xx or 3xx) response to a request whose method is not safe (unknown methods included), invalidates `uri`; returns how many responses were removed.
- **Cache.ResponseCache** · [`export let remove = func(method: string, uri: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L505)
  Removes the response stored for `method` and `uri`; returns whether one was stored.
- **Cache.ResponseCache** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L515)
  Removes every stored response.
- **Cache.ResponseCache** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache.nra#L522)
  The kind of cache, the number of entries and the capacity.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.

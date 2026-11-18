# `Std.Net.Resilience.TokenBucketLimiter`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.TokenBucketLimiter`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/token-bucket-limiter.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.TokenBucketLimiter`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TokenBucketLimiter** · [`export let TokenBucketLimiter = class implements limiters.RateLimiter`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/token-bucket-limiter.nra#L16)
  One token bucket per key. Tagged `#Final`, `#NotThreadSafe`.
- **TokenBucketLimiter.TokenBucketLimiter** · [`export let init = func(size: int, refill: int, refill_period: durations.Duration, source: Clock)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/token-bucket-limiter.nra#L31)
  A limiter whose buckets hold `size` tokens and gain `refill` tokens per `refill_period`. The arguments are checked as for `TokenBucket`.
- **TokenBucketLimiter.TokenBucketLimiter** · [`export let try_acquire = func(key: string, permits: int) -> Result<limiters.RateDecision>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/token-bucket-limiter.nra#L41)
  Takes `permits` tokens from the key's bucket.
- **TokenBucketLimiter.TokenBucketLimiter** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/token-bucket-limiter.nra#L50)
  The number of keys with a bucket.
- **TokenBucketLimiter.TokenBucketLimiter** · [`export let forget = func(key: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/token-bucket-limiter.nra#L53)
  Drops the bucket of `key`; its next request starts full. Returns whether it had one.
- **TokenBucketLimiter.TokenBucketLimiter** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/token-bucket-limiter.nra#L56)
  Drops every bucket.
- **TokenBucketLimiter.TokenBucketLimiter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/token-bucket-limiter.nra#L61)
  Text such as "TokenBucketLimiter(capacity 10, 2 keys)".
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.

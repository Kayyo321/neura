# `Std.Net.Http.Pool`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.Pool`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.Pool`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Pool** · [`export let RetryDecision = enum (Stop, RetryAfter(durations.Duration))`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L29)
  What to do after an attempt: stop with its outcome, or wait and try again.
- **Pool** · [`export let origin_key = func(url: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L34)
  The origin key of a URL: lowercase "scheme://host:port", with port 80 or 443 filled in; "" when the URL is not absolute http or https. Tagged `#Pure`, `#ThreadSafe`.
- **Pool** · [`export let HostLimits = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L72)
  Per-origin limits on exchanges in flight. Tagged `#Final`, `#NotThreadSafe`.
- **Pool.HostLimits** · [`export last default_limit: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L74)
  The limit for origins without their own.
- **Pool.HostLimits** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L86)
  Limits of 6 per origin (the common browser value), or `per_origin`. A limit below 1 traps with "invalid argument: limit".
- **Pool.HostLimits** · [`export let set_limit = func(url: string, limit: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L94)
  Gives the origin of `url` its own limit.
- **Pool.HostLimits** · [`export let limit_for = func(url: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L108)
  The limit of the origin of `url`.
- **Pool.HostLimits** · [`export let in_flight = func(url: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L119)
  The exchanges in flight to the origin of `url`.
- **Pool.HostLimits** · [`export let try_acquire = func(url: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L125)
  Takes a slot for the origin of `url`; false at the limit.
- **Pool.HostLimits** · [`export let release = func(url: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L143)
  Returns a slot; releasing an origin without a slot traps with "invalid state: release without acquire".
- **Pool.HostLimits** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L152)
  The text "HostLimits(default N)".
- **Pool** · [`export let RetryPolicy = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L167)
  When and how long to retry. Tagged `#Final`, `#NotThreadSafe`.
- **Pool.RetryPolicy** · [`export last max_attempts: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L169)
  The most attempts, the first included.
- **Pool.RetryPolicy** · [`export last base_wait: durations.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L171)
  The wait before the second attempt.
- **Pool.RetryPolicy** · [`export last max_wait: durations.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L173)
  The longest wait.
- **Pool.RetryPolicy** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L179)
  3 attempts, 100 ms doubling up to 2 s; or the given values. Fewer than 1 attempt or a negative wait traps with "invalid argument: retry policy".
- **Pool.RetryPolicy** · [`export let set_honor_idempotency_key = func(honor: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L193)
  Sets whether an `Idempotency-Key` field makes a request retryable.
- **Pool.RetryPolicy** · [`export let is_repeatable = func(request: http.ClientRequest) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L198)
  Whether `request` may be repeated after it may have reached the server.
- **Pool.RetryPolicy** · [`export let backoff = func(attempt: int) -> durations.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L203)
  The computed wait after attempt `attempt` (1-based): base times 2^(attempt-1), at most the cap.
- **Pool.RetryPolicy** · [`export let decide = func(request: http.ClientRequest, attempt: int, outcome: Result<http.Response>, now: clocks.Clock) -> RetryDecision`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L215)
  The decision after attempt `attempt` ended with `outcome`; `now` resolves an HTTP-date `Retry-After`. Tagged `#NoDiscard`.
- **Pool.RetryPolicy** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L256)
  The text "RetryPolicy(N attempts)".
- **Pool** · [`export let retry_after = func(response: http.Response, now: clocks.Clock) -> Optional<durations.Duration>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L262)
  The wait a `Retry-After` field asks for: delay-seconds, or an HTTP-date minus the clock's now (zero when past); None when absent or invalid (RFC 9110 section 10.2.3). Tagged `#NoDiscard`.
- **Pool** · [`export let PooledClient = class implements services.Service<http.ClientRequest, http.Response>, http.Transport`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L306)
  A client that limits exchanges per origin and retries by a policy, over a transport; a `Std.Net.Service<ClientRequest, Response>` and itself a `Transport`. Tagged `#Final`, `#NotThreadSafe`.
- **Pool.PooledClient** · [`export last limits: HostLimits`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L310)
  The per-origin limits.
- **Pool.PooledClient** · [`export last policy: RetryPolicy`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L312)
  The retry policy.
- **Pool.PooledClient** · [`export let init = func(carrier: http.Transport, origin_limits: HostLimits, retries: RetryPolicy, time: clocks.Clock)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L319)
  A pooled client.
- **Pool.PooledClient** · [`export let send = func(request: http.ClientRequest) -> Result<http.Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L329)
  Sends `request`: fails with "http.pool.busy" when its origin is at its limit; otherwise attempts until the policy stops, sleeping between attempts, and returns the last outcome. Tagged `#NoDiscard`.
- **Pool.PooledClient** · [`export let fetch = func(request: http.ClientRequest) -> Result<http.Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L366)
  `send`, as a transport.
- **Pool.PooledClient** · [`export let call = func(request: http.ClientRequest) -> Result<http.Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L369)
  `send`, as a service.
- **Pool.PooledClient** · [`export let attempts = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L372)
  The number of attempts the last `send` made.
- **Pool.PooledClient** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/pool.nra#L375)
  The text "PooledClient".
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.

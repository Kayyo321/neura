# `Std.Net.Resilience.LatencyWindow`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.LatencyWindow`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/latency-window.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.LatencyWindow`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **LatencyWindow** · [`export let LatencyWindow = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/latency-window.nra#L14)
  The last `capacity` latencies. Tagged `#Final`, `#NotThreadSafe`.
- **LatencyWindow.LatencyWindow** · [`export let init = func(size: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/latency-window.nra#L23)
  An empty window of `size` samples; a size below 1 traps with "invalid argument: capacity".
- **LatencyWindow.LatencyWindow** · [`export let record = func(latency: durations.Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/latency-window.nra#L31)
  Adds a sample, replacing the oldest when the window is full. A negative latency counts as zero.
- **LatencyWindow.LatencyWindow** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/latency-window.nra#L42)
  The number of samples held.
- **LatencyWindow.LatencyWindow** · [`export let percentile = func(p: int) -> optional.Optional<durations.Duration>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/latency-window.nra#L47)
  The nearest-rank `p`-th percentile, or None when the window is empty. `p` must be from 1 through 100, else the call traps with "invalid argument: percentile". Tagged `#NoDiscard`.
- **LatencyWindow.LatencyWindow** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/latency-window.nra#L72)
  Removes every sample.
- **LatencyWindow.LatencyWindow** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/latency-window.nra#L80)
  Text such as "LatencyWindow(12 of 100 samples)".
- **LatencyWindow** · [`export let of = func(capacity: int) -> LatencyWindow`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/latency-window.nra#L86)
  An empty window of `capacity` samples. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.

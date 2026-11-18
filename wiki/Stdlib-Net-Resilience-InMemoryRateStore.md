# `Std.Net.Resilience.InMemoryRateStore`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.InMemoryRateStore`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/in-memory-rate-store.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.InMemoryRateStore`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **InMemoryRateStore** · [`export let InMemoryRateStore = class implements stores.RateStore`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/in-memory-rate-store.nra#L14)
  A rate store over a hash map. Tagged `#Final`, `#NotThreadSafe`.
- **InMemoryRateStore.InMemoryRateStore** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/in-memory-rate-store.nra#L19)
  An empty store.
- **InMemoryRateStore.InMemoryRateStore** · [`export let get = func(key: string) -> Result<optional.Optional<int64>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/in-memory-rate-store.nra#L23)
  The value of `key`, or None.
- **InMemoryRateStore.InMemoryRateStore** · [`export let compare_and_set = func(key: string, expected: optional.Optional<int64>, value: int64, ttl: durations.Duration) -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/in-memory-rate-store.nra#L27)
  Stores `value` when the key holds `expected`.
- **InMemoryRateStore.InMemoryRateStore** · [`export let remove_expired = func(epoch_nanos: int64) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/in-memory-rate-store.nra#L48)
  Removes every key whose arrival time is at or before `epoch_nanos` (such a key behaves like an absent one) and returns how many it removed.
- **InMemoryRateStore.InMemoryRateStore** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/in-memory-rate-store.nra#L52)
  The number of keys.
- **InMemoryRateStore.InMemoryRateStore** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/in-memory-rate-store.nra#L55)
  Removes every key.
- **InMemoryRateStore.InMemoryRateStore** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/in-memory-rate-store.nra#L60)
  Text such as "InMemoryRateStore(3 keys)".
- **InMemoryRateStore** · [`export let create = func -> InMemoryRateStore`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/in-memory-rate-store.nra#L65)
  An empty store. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.

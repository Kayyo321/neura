# `Std.Net.Core.PrefixTrie`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Core.PrefixTrie`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/prefix-trie.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Core.PrefixTrie`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **PrefixTrie** · [`export let PrefixTrie = class<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/prefix-trie.nra#L19)
  A mutable map from `CidrBlock` keys to values of type `V`. Tagged `#Final`. Since 0.3.0.
- **PrefixTrie.PrefixTrie** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/prefix-trie.nra#L30)
  Creates an empty table: node 0 is the IPv4 root and node 1 the IPv6 root.
- **PrefixTrie.PrefixTrie** · [`export let insert = func(block: cidr.CidrBlock, value: V) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/prefix-trie.nra#L44)
  Stores `value` for `block`; returns true when the block was new and false when its value was replaced.
- **PrefixTrie.PrefixTrie** · [`export let remove = func(block: cidr.CidrBlock) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/prefix-trie.nra#L71)
  Removes the value of `block`; returns whether there was one.
- **PrefixTrie.PrefixTrie** · [`export let longest_match = func(ip: ips.IpAddr) -> Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/prefix-trie.nra#L83)
  The value of the longest stored block that contains `ip`, or None. Tagged `#NoDiscard`.
- **PrefixTrie.PrefixTrie** · [`export let longest_match_block = func(ip: ips.IpAddr) -> Optional<cidr.CidrBlock>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/prefix-trie.nra#L93)
  The longest stored block that contains `ip`, or None. Tagged `#NoDiscard`.
- **PrefixTrie.PrefixTrie** · [`export let exact = func(block: cidr.CidrBlock) -> Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/prefix-trie.nra#L103)
  The value stored for exactly `block`, or None. Tagged `#NoDiscard`.
- **PrefixTrie.PrefixTrie** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/prefix-trie.nra#L112)
  The number of stored blocks.
- **PrefixTrie.PrefixTrie** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/prefix-trie.nra#L115)
  `PrefixTrie(size N)`; the entries are not listed.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.

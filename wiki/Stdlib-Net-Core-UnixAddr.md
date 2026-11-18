# `Std.Net.Core.UnixAddr`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Core.UnixAddr`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/unix-addr.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Core.UnixAddr`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **UnixAddr** · [`export let UnixAddr = class implements NetAddress`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/unix-addr.nra#L24)
  A Unix-domain socket address. Tagged `#Final`, `#NoDiscard`, `#Immutable`, `#ThreadSafe`. Since 0.3.0.
- **UnixAddr.UnixAddr** · [`export last path: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/unix-addr.nra#L26)
  The filesystem path, or the abstract name without its leading NUL.
- **UnixAddr.UnixAddr** · [`export last is_abstract: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/unix-addr.nra#L28)
  True for a Linux abstract-namespace name.
- **UnixAddr.UnixAddr** · [`export let init = func(name: string, abstract_namespace: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/unix-addr.nra#L32)
  Constructs the address; traps when the name is empty or longer than 107 bytes, or when a path contains a NUL byte.
- **UnixAddr.UnixAddr** · [`export let family = func -> AddressFamily`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/unix-addr.nra#L43)
  Always `AddressFamily.Unix`. Tagged `#Pure`.
- **UnixAddr.UnixAddr** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/unix-addr.nra#L46)
  Tests whether another object is a `UnixAddr` of the same kind and name.
- **UnixAddr.UnixAddr** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/unix-addr.nra#L55)
  Combines the kind with the name's hash. Tagged `#Pure`.
- **UnixAddr.UnixAddr** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/unix-addr.nra#L60)
  The path, or `@` followed by the abstract name. Tagged `#Pure`.
- **UnixAddr** · [`export let of_path = func(path: string) -> UnixAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/unix-addr.nra#L66)
  The address of a filesystem path; traps when the path is empty, longer than 107 bytes, or contains NUL. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **UnixAddr** · [`export let abstract_name = func(name: string) -> UnixAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/unix-addr.nra#L71)
  The address of a Linux abstract-namespace name (given without the leading NUL); traps when the name is empty or longer than 107 bytes. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **UnixAddr** · [`export let parse = func(text: string) -> Result<UnixAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/unix-addr.nra#L78)
  Parses `@name` as an abstract name and any other text as a path. Errors: Empty for "" or a lone `@`; OutOfRange at offset 107 (108 for `@name`) when the name is too long; InvalidCharacter at a NUL byte in a path. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.

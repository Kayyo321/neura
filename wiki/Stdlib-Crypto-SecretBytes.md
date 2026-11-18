# `Std.Crypto.Core.SecretBytes`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Core.SecretBytes`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Core.SecretBytes`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **SecretBytes** · [`export let SecretBytes = class implements SecretMemory<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L13)
  A private copy of secret bytes. `to_string` prints a fixed placeholder and never the bytes; `equals` and `hash_code` are the identity of the object, and contents are compared only with the branch-free `ct_equals`. `wipe` overwrites the copy this object holds with zeros. The runtime cannot lock the pages, keep them out of swap, or erase copies the collector made; `lock` is a stub until it can. Tagged `#Final`. Since 0.3.0.
- **SecretBytes.SecretBytes** · [`export let init = func(material: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L20)
  Copies the bytes, so the caller may wipe or reuse its own list.
- **SecretBytes.SecretBytes** · [`export let length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L27)
  The number of bytes; still readable after `wipe`.
- **SecretBytes.SecretBytes** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L30)
  The held list itself, not a copy. Traps with "invalid state: secret wiped" after `wipe`.
- **SecretBytes.SecretBytes** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L38)
  Overwrites every held byte with zero and marks the secret wiped. A second call does nothing.
- **SecretBytes.SecretBytes** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L46)
  Whether `wipe` has run.
- **SecretBytes.SecretBytes** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L50)
  Locks the pages in memory and out of swap.
- **SecretBytes.SecretBytes** · [`export let ct_equals = func(other: SecretBytes) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L58)
  Whether `other` holds the same bytes, compared without a data-dependent branch. Traps if either is wiped. Tagged `#NoDiscard`.
- **SecretBytes.SecretBytes** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L61)
  A fixed placeholder with the length, never the bytes.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#202-stdcryptocore). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.

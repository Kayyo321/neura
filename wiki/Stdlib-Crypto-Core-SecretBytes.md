# `Std.Crypto.Core.SecretBytes`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Core.SecretBytes`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Core.SecretBytes`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **SecretBytes** · [`export let SecretBytes = class implements SecretMemory<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L20)
  A private copy of secret bytes with identity equality and a fixed redacted display. A successful lock moves the held bytes to a dedicated host buffer and overwrites the original list. Host guarantees cover only that buffer. Copies exposed to Neura remain ordinary heap bytes. Tagged `#Final`. Since 0.3.0.
- **SecretBytes.SecretBytes** · [`export let init = func(material: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L29)
  Copies the material, so the caller can wipe or reuse its own list. Since 0.3.0.
- **SecretBytes.SecretBytes** · [`export let length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L38)
  The retained byte count, including after wiping. Since 0.3.0.
- **SecretBytes.SecretBytes** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L44)
  Returns the held list before locking and a new ordinary heap copy after locking. Traps with "invalid state: secret wiped" after wiping. Since 0.3.0.
- **SecretBytes.SecretBytes** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L63)
  Overwrites the held heap bytes or releases the locked host buffer once. Since 0.3.0.
- **SecretBytes.SecretBytes** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L81)
  Whether wiping has completed. Since 0.3.0.
- **SecretBytes.SecretBytes** · [`export let try_lock = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L88)
  Moves 1 to 65,536 bytes into a locked host buffer. Failure preserves the original material. Repeated successful calls are idempotent. Locking needs the entropy capability and a provider that can exclude the pages from swap, core dumps and forked children. Recording runs refuse it. Tagged `#NoDiscard`. Since 0.3.0.
- **SecretBytes.SecretBytes** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L123)
  Moves the held bytes into locked memory and traps if the provider refuses the request. Since 0.3.0.
- **SecretBytes.SecretBytes** · [`export let ct_equals = func(other: SecretBytes) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L136)
  Compares secret contents. Either wiped input traps. The software comparison is branch-free in source. The runtime does not guarantee constant time. Tagged `#NoDiscard`. Since 0.3.0.
- **SecretBytes.SecretBytes** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-bytes.nra#L156)
  Fixed display for secret bytes. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#212-stdcryptocore). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.

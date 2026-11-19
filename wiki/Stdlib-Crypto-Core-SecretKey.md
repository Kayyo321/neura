# `Std.Crypto.Core.SecretKey`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Core.SecretKey`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-key.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Core.SecretKey`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **SecretKey** · [`export let SecretKey = class<Alg> implements SymmetricKey<Alg>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-key.nra#L11)
  Key bytes in a `SecretBytes`, for the primitive class `Alg`: a `SecretKey<AesGcm>` is accepted by `AesGcm` and by nothing else. Printing, equality and wiping are those of `SecretBytes`. Tagged `#Final`. Since 0.3.0.
- **SecretKey.SecretKey** · [`export let init = func(key: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-key.nra#L16)
  Copies the key bytes.
- **SecretKey.SecretKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-key.nra#L20)
- **SecretKey.SecretKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-key.nra#L21)
- **SecretKey.SecretKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-key.nra#L22)
- **SecretKey.SecretKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-key.nra#L23)
- **SecretKey.SecretKey** · [`export let key_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-key.nra#L24)
- **SecretKey.SecretKey** · [`export let ct_equals = func(other: SecretKey<Alg>) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-key.nra#L28)
  Whether `other` holds the same key bytes, compared without a data-dependent branch. Tagged `#NoDiscard`.
- **SecretKey.SecretKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret-key.nra#L31)
  A fixed redacted placeholder.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#212-stdcryptocore). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.

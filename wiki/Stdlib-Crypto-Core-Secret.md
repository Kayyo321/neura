# `Std.Crypto.Core.Secret`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Core.Secret`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Core.Secret`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Secret** · [`export let Secret = class<T> implements SecretMemory<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L12)
  Wraps a value of type T so that it is never printed. Because a generic class cannot overwrite a value of unknown type, the constructor takes an `eraser` that `wipe` calls with the value (for a byte list, a function that zeroes it). Generic values remain in the Neura heap. Use `SecretBytes` for the dedicated byte-buffer locking service. Tagged `#Final`. Since 0.3.0.
- **Secret.Secret** · [`export let init = func(secret: T, erase: func(T) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L21)
  Wraps the value, with the function `wipe` uses to erase it.
- **Secret.Secret** · [`export let expose = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L27)
  The value. Traps with "invalid state: secret wiped" after `wipe`.
- **Secret.Secret** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L35)
  Calls the eraser once and marks the secret wiped.
- **Secret.Secret** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L43)
  Whether `wipe` has run.
- **Secret.Secret** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L49)
  Requests locking for a generic value. Since 0.3.0.
- **Secret.Secret** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L55)
  A fixed placeholder, never the value.
- **Secret** · [`export let zero_bytes = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L60)
  An eraser for byte lists: overwrites every byte with zero. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#212-stdcryptocore). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.

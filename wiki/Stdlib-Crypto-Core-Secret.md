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

- **Secret** · [`export let Secret = class<T> implements SecretMemory<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L28)
  Wraps a value of type T so that it is never printed. Because a generic class cannot overwrite a value of unknown type, the constructor takes an `eraser` that `wipe` calls with the value (for a byte list, a function that zeroes it). Locking protects only a serialized copy in a dedicated host buffer. The original T passed to the constructor, its aliases, previously exposed values and caller-owned copies are NOT locked or erased by locking; they retain their ordinary heap lifetimes. The caller must erase or retire those values separately. This API does not transfer ownership of the original. Tagged `#Final`. Since 0.3.0.
- **Secret.Secret** · [`export let init = func(secret: T, erase: func(T) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L39)
  Wraps the value, with the function `wipe` uses to erase it.
- **Secret.Secret** · [`export let expose = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L45)
  The value. Traps with "invalid state: secret wiped" after `wipe`.
- **Secret.Secret** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L62)
  Calls the eraser once and marks the secret wiped.
- **Secret.Secret** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L76)
  Whether `wipe` has run.
- **Secret.Secret** · [`export let set_lock_codec = func(encode: func(T) -> uint8[list], decode: func(uint8[list]) -> T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L83)
  Sets the codec for making a locked serialized copy of this secret's representation. The encoder returns a fresh owned byte list. The decoder returns an independent value and must not retain its input. Neither callback should capture copies of the secret. Call before locking. A successful lock may change the identity of subsequently exposed values. Since 0.3.1.
- **Secret.Secret** · [`export let try_lock = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L95)
  Retains a serialized copy in locked host memory. Failure retains the original value. Success releases this wrapper's reference to T without erasing or locking T or its aliases. Uses the entropy capability and refuses in a recording run. Repeated successful calls are idempotent. Tagged `#NoDiscard`. Since 0.3.1.
- **Secret.Secret** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L127)
  Locks a serialized copy using the configured codec, not the original T or any aliases. Traps with the provider or codec diagnostic if locking fails. Since 0.3.0.
- **Secret.Secret** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L143)
  A fixed placeholder, never the value.
- **Secret** · [`export let zero_bytes = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/secret.nra#L148)
  An eraser for byte lists: overwrites every byte with zero. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#212-stdcryptocore). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.

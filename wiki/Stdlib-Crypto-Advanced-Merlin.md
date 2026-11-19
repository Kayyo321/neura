# `Std.Crypto.Advanced.Merlin`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Advanced.Merlin` to use this module. The [package specification](Stdlib-Design#2113-stdcryptoadvanced) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/merlin.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Merlin** · [`export let Transcript = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/merlin.nra#L18)
  A mutable transcript with an application domain label and independent STROBE state. Tagged `#Final`. Since 0.3.0.
- **Merlin.Transcript** · [`export let init = func(application: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/merlin.nra#L27)
  Starts a transcript and appends the application label under dom-sep. Since 0.3.0.
- **Merlin.Transcript** · [`export let append_message = func(label: uint8[list], message: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/merlin.nra#L91)
  Appends a labeled byte string, framing its length as uint32 little-endian. Since 0.3.0.
- **Merlin.Transcript** · [`export let append_u64 = func(label: uint8[list], value: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/merlin.nra#L100)
  Appends a labeled uint64 in little-endian order. Since 0.3.0.
- **Merlin.Transcript** · [`export let challenge_bytes = func(label: uint8[list], length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/merlin.nra#L110)
  Derives a challenge at the current state and advances the transcript. A negative length traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Merlin.Transcript** · [`export let fork = func -> Transcript`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/merlin.nra#L128)
  Creates an independent copy at the same transcript position. Tagged `#NoDiscard`. Since 0.3.0.
- **Merlin.Transcript** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/merlin.nra#L139)
  Omits the transcript state. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)

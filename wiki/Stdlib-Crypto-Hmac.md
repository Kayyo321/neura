# `Std.Crypto.Mac.Hmac`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Mac.Hmac`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/hmac.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Mac.Hmac`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Hmac** · [`export let Hmac = class<H: StreamingDigest<H>> implements Authenticator`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/hmac.nra#L15)
  HMAC with the digest H, such as `Hmac<Sha256>`. A key longer than H's block is hashed first; a shorter one is padded with zeros. The policy is checked for HMAC and for H. Tagged `#Final`. Since 0.3.0.
- **Hmac.Hmac** · [`export let init = func(key: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/hmac.nra#L23)
  Keys the MAC. The caller may wipe its key afterwards.
- **Hmac.Hmac** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/hmac.nra#L44)
  `Algorithm.Hmac`.
- **Hmac.Hmac** · [`export let tag_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/hmac.nra#L47)
  H's output length.
- **Hmac.Hmac** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/hmac.nra#L49)
- **Hmac.Hmac** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/hmac.nra#L56)
- **Hmac.Hmac** · [`export let verify = func(tag: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/hmac.nra#L68)
- **Hmac.Hmac** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/hmac.nra#L70)
- **Hmac** · [`export let mac = func<H: StreamingDigest<H>>(key: uint8[list], message: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/hmac.nra#L75)
  The HMAC tag of the message under the key. Tagged `#NoDiscard`. Since 0.3.0.
- **Hmac** · [`export let verify = func<H: StreamingDigest<H>>(key: uint8[list], message: uint8[list], tag: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/hmac.nra#L83)
  Whether `tag` is the full HMAC tag of the message, compared in constant time. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#205-stdcryptomac). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.

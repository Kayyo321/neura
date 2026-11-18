# `Std.Crypto.Hash.Sha2`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Hash.Sha2`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Hash.Sha2`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Sha2** · [`export let Sha224 = class implements StreamingDigest<Sha224>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L19)
  A running SHA-224 digest. `finish` returns 28 bytes; an `update` or `finish` after it traps with "invalid state: digest finished". Tagged `#Final`. Since 0.3.0.
- **Sha2.Sha224** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L24)
  Checks the policy.
- **Sha2.Sha224** · [`export static let create = func() -> Sha224`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L29)
  A new digest with no bytes added.
- **Sha2.Sha224** · [`export static let output_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L32)
  28.
- **Sha2.Sha224** · [`export static let block_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L35)
  64.
- **Sha2.Sha224** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L38)
  `Algorithm.Sha224`.
- **Sha2.Sha224** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L41)
  Adds the bytes to the message.
- **Sha2.Sha224** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L46)
  Pads the message, ends the digest, and returns the 28-byte hash.
- **Sha2.Sha224** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L48)
- **Sha2** · [`export let sha224 = func(bytes: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L53)
  The SHA-224 hash of the bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Sha2** · [`export let Sha256 = class implements StreamingDigest<Sha256>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L62)
  A running SHA-256 digest. `finish` returns 32 bytes; an `update` or `finish` after it traps with "invalid state: digest finished". Tagged `#Final`. Since 0.3.0.
- **Sha2.Sha256** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L67)
  Checks the policy.
- **Sha2.Sha256** · [`export static let create = func() -> Sha256`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L72)
  A new digest with no bytes added.
- **Sha2.Sha256** · [`export static let output_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L75)
  32.
- **Sha2.Sha256** · [`export static let block_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L78)
  64.
- **Sha2.Sha256** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L81)
  `Algorithm.Sha256`.
- **Sha2.Sha256** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L84)
  Adds the bytes to the message.
- **Sha2.Sha256** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L89)
  Pads the message, ends the digest, and returns the 32-byte hash.
- **Sha2.Sha256** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L91)
- **Sha2** · [`export let sha256 = func(bytes: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L96)
  The SHA-256 hash of the bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Sha2** · [`export let Sha384 = class implements StreamingDigest<Sha384>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L105)
  A running SHA-384 digest. `finish` returns 48 bytes; an `update` or `finish` after it traps with "invalid state: digest finished". Tagged `#Final`. Since 0.3.0.
- **Sha2.Sha384** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L110)
  Checks the policy.
- **Sha2.Sha384** · [`export static let create = func() -> Sha384`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L115)
  A new digest with no bytes added.
- **Sha2.Sha384** · [`export static let output_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L118)
  48.
- **Sha2.Sha384** · [`export static let block_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L121)
  128.
- **Sha2.Sha384** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L124)
  `Algorithm.Sha384`.
- **Sha2.Sha384** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L127)
  Adds the bytes to the message.
- **Sha2.Sha384** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L132)
  Pads the message, ends the digest, and returns the 48-byte hash.
- **Sha2.Sha384** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L134)
- **Sha2** · [`export let sha384 = func(bytes: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L139)
  The SHA-384 hash of the bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Sha2** · [`export let Sha512 = class implements StreamingDigest<Sha512>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L148)
  A running SHA-512 digest. `finish` returns 64 bytes; an `update` or `finish` after it traps with "invalid state: digest finished". Tagged `#Final`. Since 0.3.0.
- **Sha2.Sha512** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L153)
  Checks the policy.
- **Sha2.Sha512** · [`export static let create = func() -> Sha512`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L158)
  A new digest with no bytes added.
- **Sha2.Sha512** · [`export static let output_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L161)
  64.
- **Sha2.Sha512** · [`export static let block_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L164)
  128.
- **Sha2.Sha512** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L167)
  `Algorithm.Sha512`.
- **Sha2.Sha512** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L170)
  Adds the bytes to the message.
- **Sha2.Sha512** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L175)
  Pads the message, ends the digest, and returns the 64-byte hash.
- **Sha2.Sha512** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L177)
- **Sha2** · [`export let sha512 = func(bytes: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L182)
  The SHA-512 hash of the bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Sha2** · [`export let Sha512_256 = class implements StreamingDigest<Sha512_256>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L191)
  A running SHA-512/256 digest. `finish` returns 32 bytes; an `update` or `finish` after it traps with "invalid state: digest finished". Tagged `#Final`. Since 0.3.0.
- **Sha2.Sha512_256** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L196)
  Checks the policy.
- **Sha2.Sha512_256** · [`export static let create = func() -> Sha512_256`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L201)
  A new digest with no bytes added.
- **Sha2.Sha512_256** · [`export static let output_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L204)
  32.
- **Sha2.Sha512_256** · [`export static let block_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L207)
  128.
- **Sha2.Sha512_256** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L210)
  `Algorithm.Sha512_256`.
- **Sha2.Sha512_256** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L213)
  Adds the bytes to the message.
- **Sha2.Sha512_256** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L218)
  Pads the message, ends the digest, and returns the 32-byte hash.
- **Sha2.Sha512_256** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L220)
- **Sha2** · [`export let sha512_256 = func(bytes: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha2.nra#L225)
  The SHA-512/256 hash of the bytes. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#204-stdcryptohash). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.

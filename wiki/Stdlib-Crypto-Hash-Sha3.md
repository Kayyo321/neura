# `Std.Crypto.Hash.Sha3`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Hash.Sha3`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Hash.Sha3`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Sha3** · [`export let Sha3_224 = class implements StreamingDigest<Sha3_224>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L22)
  A running SHA3-224 digest: 28 bytes of output, 144-byte rate. Tagged `#Final`. Since 0.3.0.
- **Sha3.Sha3_224** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L27)
  Checks the policy.
- **Sha3.Sha3_224** · [`export static let create = func() -> Sha3_224`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L32)
  A new digest with no bytes added.
- **Sha3.Sha3_224** · [`export static let output_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L34)
  28.
- **Sha3.Sha3_224** · [`export static let block_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L36)
  The rate, 144.
- **Sha3.Sha3_224** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L38)
  `Algorithm.Sha3_224`.
- **Sha3.Sha3_224** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L41)
  Adds the bytes to the message.
- **Sha3.Sha3_224** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L46)
  Ends the digest and returns the 28-byte hash. A second call traps.
- **Sha3.Sha3_224** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L53)
- **Sha3** · [`export let sha3_224 = func(bytes: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L58)
  The SHA3-224 hash of the bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Sha3** · [`export let Sha3_256 = class implements StreamingDigest<Sha3_256>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L66)
  A running SHA3-256 digest: 32 bytes of output, 136-byte rate. Tagged `#Final`. Since 0.3.0.
- **Sha3.Sha3_256** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L71)
  Checks the policy.
- **Sha3.Sha3_256** · [`export static let create = func() -> Sha3_256`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L76)
  A new digest with no bytes added.
- **Sha3.Sha3_256** · [`export static let output_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L78)
  32.
- **Sha3.Sha3_256** · [`export static let block_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L80)
  The rate, 136.
- **Sha3.Sha3_256** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L82)
  `Algorithm.Sha3_256`.
- **Sha3.Sha3_256** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L85)
  Adds the bytes to the message.
- **Sha3.Sha3_256** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L90)
  Ends the digest and returns the 32-byte hash. A second call traps.
- **Sha3.Sha3_256** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L97)
- **Sha3** · [`export let sha3_256 = func(bytes: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L102)
  The SHA3-256 hash of the bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Sha3** · [`export let Sha3_384 = class implements StreamingDigest<Sha3_384>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L110)
  A running SHA3-384 digest: 48 bytes of output, 104-byte rate. Tagged `#Final`. Since 0.3.0.
- **Sha3.Sha3_384** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L115)
  Checks the policy.
- **Sha3.Sha3_384** · [`export static let create = func() -> Sha3_384`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L120)
  A new digest with no bytes added.
- **Sha3.Sha3_384** · [`export static let output_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L122)
  48.
- **Sha3.Sha3_384** · [`export static let block_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L124)
  The rate, 104.
- **Sha3.Sha3_384** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L126)
  `Algorithm.Sha3_384`.
- **Sha3.Sha3_384** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L129)
  Adds the bytes to the message.
- **Sha3.Sha3_384** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L134)
  Ends the digest and returns the 48-byte hash. A second call traps.
- **Sha3.Sha3_384** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L141)
- **Sha3** · [`export let sha3_384 = func(bytes: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L146)
  The SHA3-384 hash of the bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Sha3** · [`export let Sha3_512 = class implements StreamingDigest<Sha3_512>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L154)
  A running SHA3-512 digest: 64 bytes of output, 72-byte rate. Tagged `#Final`. Since 0.3.0.
- **Sha3.Sha3_512** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L159)
  Checks the policy.
- **Sha3.Sha3_512** · [`export static let create = func() -> Sha3_512`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L164)
  A new digest with no bytes added.
- **Sha3.Sha3_512** · [`export static let output_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L166)
  64.
- **Sha3.Sha3_512** · [`export static let block_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L168)
  The rate, 72.
- **Sha3.Sha3_512** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L170)
  `Algorithm.Sha3_512`.
- **Sha3.Sha3_512** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L173)
  Adds the bytes to the message.
- **Sha3.Sha3_512** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L178)
  Ends the digest and returns the 64-byte hash. A second call traps.
- **Sha3.Sha3_512** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L185)
- **Sha3** · [`export let sha3_512 = func(bytes: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L190)
  The SHA3-512 hash of the bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Sha3** · [`export let Shake128 = class implements ExtendableOutput<Shake128>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L198)
  SHAKE128. An extendable-output function: `squeeze` and `read` continue the output stream. Tagged `#Final`. Since 0.3.0.
- **Sha3.Shake128** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L203)
  Checks the policy.
- **Sha3.Shake128** · [`export static let create = func() -> Shake128`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L209)
  A new function with no bytes added.
- **Sha3.Shake128** · [`export static let output_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L211)
  The length of `finish`'s result, 32: the output at the function's security strength.
- **Sha3.Shake128** · [`export static let block_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L213)
  The rate, 168.
- **Sha3.Shake128** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L215)
  `Algorithm.Shake128`.
- **Sha3.Shake128** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L218)
  Adds the bytes to the input. Traps with "invalid state: digest finished" once output was read.
- **Sha3.Shake128** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L223)
  The first 32 bytes of output (or the next 32, if output was already read).
- **Sha3.Shake128** · [`export let squeeze = func(count: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L226)
  The next `count` bytes of output.
- **Sha3.Shake128** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L228)
- **Sha3** · [`export let shake128 = func(bytes: uint8[list], length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L233)
  The first `length` bytes of SHAKE128 of the bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Sha3** · [`export let Shake256 = class implements ExtendableOutput<Shake256>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L241)
  SHAKE256. An extendable-output function: `squeeze` and `read` continue the output stream. Tagged `#Final`. Since 0.3.0.
- **Sha3.Shake256** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L246)
  Checks the policy.
- **Sha3.Shake256** · [`export static let create = func() -> Shake256`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L252)
  A new function with no bytes added.
- **Sha3.Shake256** · [`export static let output_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L254)
  The length of `finish`'s result, 64: the output at the function's security strength.
- **Sha3.Shake256** · [`export static let block_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L256)
  The rate, 136.
- **Sha3.Shake256** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L258)
  `Algorithm.Shake256`.
- **Sha3.Shake256** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L261)
  Adds the bytes to the input. Traps with "invalid state: digest finished" once output was read.
- **Sha3.Shake256** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L266)
  The first 64 bytes of output (or the next 64, if output was already read).
- **Sha3.Shake256** · [`export let squeeze = func(count: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L269)
  The next `count` bytes of output.
- **Sha3.Shake256** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L271)
- **Sha3** · [`export let shake256 = func(bytes: uint8[list], length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L276)
  The first `length` bytes of SHAKE256 of the bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Sha3** · [`export let CShake128 = class implements ExtendableOutput<CShake128>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L284)
  cSHAKE128 (SP 800-185, section 3). With an empty function name N and customization S it is SHAKE128; otherwise the sponge first absorbs bytepad(encode_string(N) || encode_string(S)) and pads with 0x04. Tagged `#Final`. Since 0.3.0.
- **Sha3.CShake128** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L289)
  cSHAKE128 with empty function name and customization.
- **Sha3.CShake128** · [`export static let create = func() -> CShake128`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L298)
  A new function with no bytes added, with empty function name and customization (then it equals SHAKE128).
- **Sha3.CShake128** · [`export static let output_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L300)
  The length of `finish`'s result, 32: the output at the function's security strength.
- **Sha3.CShake128** · [`export static let block_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L302)
  The rate, 168.
- **Sha3.CShake128** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L304)
  `Algorithm.CShake128`.
- **Sha3.CShake128** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L307)
  Adds the bytes to the input. Traps with "invalid state: digest finished" once output was read.
- **Sha3.CShake128** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L312)
  The first 32 bytes of output (or the next 32, if output was already read).
- **Sha3.CShake128** · [`export let squeeze = func(count: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L315)
  The next `count` bytes of output.
- **Sha3.CShake128** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L317)
- **Sha3** · [`export let cshake128 = func(bytes: uint8[list], length: int, function_name: uint8[list], customization: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L322)
  The first `length` bytes of cSHAKE128(bytes, length, function_name, customization). Tagged `#NoDiscard`. Since 0.3.0.
- **Sha3** · [`export let CShake256 = class implements ExtendableOutput<CShake256>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L331)
  cSHAKE256 (SP 800-185, section 3). With an empty function name N and customization S it is SHAKE256; otherwise the sponge first absorbs bytepad(encode_string(N) || encode_string(S)) and pads with 0x04. Tagged `#Final`. Since 0.3.0.
- **Sha3.CShake256** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L336)
  cSHAKE256 with empty function name and customization.
- **Sha3.CShake256** · [`export static let create = func() -> CShake256`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L345)
  A new function with no bytes added, with empty function name and customization (then it equals SHAKE256).
- **Sha3.CShake256** · [`export static let output_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L347)
  The length of `finish`'s result, 64: the output at the function's security strength.
- **Sha3.CShake256** · [`export static let block_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L349)
  The rate, 136.
- **Sha3.CShake256** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L351)
  `Algorithm.CShake256`.
- **Sha3.CShake256** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L354)
  Adds the bytes to the input. Traps with "invalid state: digest finished" once output was read.
- **Sha3.CShake256** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L359)
  The first 64 bytes of output (or the next 64, if output was already read).
- **Sha3.CShake256** · [`export let squeeze = func(count: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L362)
  The next `count` bytes of output.
- **Sha3.CShake256** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L364)
- **Sha3** · [`export let cshake256 = func(bytes: uint8[list], length: int, function_name: uint8[list], customization: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L369)
  The first `length` bytes of cSHAKE256(bytes, length, function_name, customization). Tagged `#NoDiscard`. Since 0.3.0.
- **Sha3** · [`export let start = func(rate: int, function_name: uint8[list], customization: uint8[list]) -> keccak.Sponge`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L379)
  A cSHAKE sponge of the rate: SHAKE's when N and S are empty, otherwise primed with bytepad(encode_string(N) || encode_string(S), rate) and padded with 0x04. Also used by TupleHash and KMAC. Since 0.3.0.
- **Sha3** · [`export let TurboShake128 = class implements ExtendableOutput<TurboShake128>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L391)
  TurboSHAKE128 (RFC 9861). The Keccak-p[1600, 12] sponge with a domain byte D from 0x01 to 0x7F (0x1F by default). Tagged `#Final`. Since 0.3.0.
- **Sha3.TurboShake128** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L396)
  TurboSHAKE128 with the domain byte 0x1F.
- **Sha3.TurboShake128** · [`export static let create = func() -> TurboShake128`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L408)
  A new function with no bytes added, with the domain byte 0x1F.
- **Sha3.TurboShake128** · [`export static let output_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L410)
  The length of `finish`'s result, 32: the output at the function's security strength.
- **Sha3.TurboShake128** · [`export static let block_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L412)
  The rate, 168.
- **Sha3.TurboShake128** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L414)
  `Algorithm.TurboShake128`.
- **Sha3.TurboShake128** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L417)
  Adds the bytes to the input. Traps with "invalid state: digest finished" once output was read.
- **Sha3.TurboShake128** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L422)
  The first 32 bytes of output (or the next 32, if output was already read).
- **Sha3.TurboShake128** · [`export let squeeze = func(count: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L425)
  The next `count` bytes of output.
- **Sha3.TurboShake128** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L427)
- **Sha3** · [`export let TurboShake256 = class implements ExtendableOutput<TurboShake256>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L432)
  TurboSHAKE256 (RFC 9861). The Keccak-p[1600, 12] sponge with a domain byte D from 0x01 to 0x7F (0x1F by default). Tagged `#Final`. Since 0.3.0.
- **Sha3.TurboShake256** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L437)
  TurboSHAKE256 with the domain byte 0x1F.
- **Sha3.TurboShake256** · [`export static let create = func() -> TurboShake256`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L449)
  A new function with no bytes added, with the domain byte 0x1F.
- **Sha3.TurboShake256** · [`export static let output_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L451)
  The length of `finish`'s result, 64: the output at the function's security strength.
- **Sha3.TurboShake256** · [`export static let block_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L453)
  The rate, 136.
- **Sha3.TurboShake256** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L455)
  `Algorithm.TurboShake256`.
- **Sha3.TurboShake256** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L458)
  Adds the bytes to the input. Traps with "invalid state: digest finished" once output was read.
- **Sha3.TurboShake256** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L463)
  The first 64 bytes of output (or the next 64, if output was already read).
- **Sha3.TurboShake256** · [`export let squeeze = func(count: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L466)
  The next `count` bytes of output.
- **Sha3.TurboShake256** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/sha3.nra#L468)
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#214-stdcryptohash). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.

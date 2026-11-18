# `Std.Crypto.Symmetric.OnlineCipher`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Symmetric.OnlineCipher`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/online-cipher.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Symmetric.OnlineCipher`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **OnlineCipher** · [`export let StreamEncryptor = class<K>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/online-cipher.nra#L44)
  Seals a stream segment by segment. Tagged `#Final`. Since 0.3.0.
- **OnlineCipher.StreamEncryptor** · [`export let init = func(aead: AeadCipher<K, uint8[list], uint8[list]>, nonce_prefix: uint8[list], associated_data: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/online-cipher.nra#L53)
  A stream under the AEAD with the nonce prefix (the AEAD's nonce length less 5 bytes) and the associated data that every segment authenticates.
- **OnlineCipher.StreamEncryptor** · [`export let seal_segment = func(plaintext: uint8[list], last_segment: bool) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/online-cipher.nra#L65)
  The sealed next segment; `last_segment` marks the final one. A segment after the final one, or a 2^32nd segment, traps. Tagged `#NoDiscard`.
- **OnlineCipher.StreamEncryptor** · [`export let is_ended = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/online-cipher.nra#L79)
  Whether the final segment was sealed.
- **OnlineCipher.StreamEncryptor** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/online-cipher.nra#L81)
- **OnlineCipher** · [`export let StreamDecryptor = class<K>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/online-cipher.nra#L86)
  Opens a stream segment by segment, in order. Tagged `#Final`. Since 0.3.0.
- **OnlineCipher.StreamDecryptor** · [`export let init = func(aead: AeadCipher<K, uint8[list], uint8[list]>, nonce_prefix: uint8[list], associated_data: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/online-cipher.nra#L95)
  The decryptor matching a StreamEncryptor with the same AEAD key, prefix and data.
- **OnlineCipher.StreamDecryptor** · [`export let open_segment = func(sealed: uint8[list], last_segment: bool) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/online-cipher.nra#L108)
  The plaintext of the next segment; `last_segment` says whether the caller's framing shows it is the final one. A segment that fails, a segment after the final one, and every call after a failure give the "crypto.authentication" error. Tagged `#NoDiscard`.
- **OnlineCipher.StreamDecryptor** · [`export let finish = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/online-cipher.nra#L129)
  `Ok` once the final segment opened; the "crypto.authentication" error for a stream that was cut short or failed. Tagged `#NoDiscard`.
- **OnlineCipher.StreamDecryptor** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/online-cipher.nra#L136)
- **OnlineCipher** · [`export let seal_segments = func<K>(aead: AeadCipher<K, uint8[list], uint8[list]>, nonce_prefix: uint8[list], associated_data: uint8[list], plaintext: uint8[list], segment_length: int) -> uint8[list][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/online-cipher.nra#L142)
  The plaintext cut into segments of `segment_length` bytes (the last may be shorter; an empty plaintext is one empty segment), each sealed. Tagged `#NoDiscard`. Since 0.3.0.
- **OnlineCipher** · [`export let open_segments = func<K>(aead: AeadCipher<K, uint8[list], uint8[list]>, nonce_prefix: uint8[list], associated_data: uint8[list], segments: uint8[list][list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/online-cipher.nra#L164)
  The plaintext of a whole stream, or the "crypto.authentication" error. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#217-stdcryptosymmetric). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.

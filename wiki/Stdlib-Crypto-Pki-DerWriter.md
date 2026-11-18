# `Std.Crypto.Pki.DerWriter`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.DerWriter` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **DerWriter** · [`export let tlv = func(tag_class: int, constructed: bool, number: int, content: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L17)
  The element with the tag class (0 to 3), construction, tag number and content. Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let sequence = func(items: uint8[list][list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L71)
  SEQUENCE of the encoded items, in order. Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let set = func(items: uint8[list][list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L75)
  SET of the encoded items, sorted into DER order. Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let explicit = func(number: int, inner: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L105)
  [number] EXPLICIT around an encoded element. Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let implicit = func(number: int, constructed: bool, content: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L109)
  [number] IMPLICIT with the content (primitive unless `constructed`). Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let boolean = func(value: bool) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L114)
  BOOLEAN (0xFF for true). Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let null = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L118)
  NULL. Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let integer = func(value: int64) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L122)
  INTEGER with the value. Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let unsigned_integer = func(magnitude: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L159)
  INTEGER with the non-negative big-endian magnitude (leading zeros are removed, and a zero byte is added before a high bit). Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let oid = func(value: Oid) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L180)
  OBJECT IDENTIFIER. Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let bit_string = func(value: BitString) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L184)
  BIT STRING. Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let bit_string_bytes = func(bytes: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L194)
  BIT STRING of whole bytes (no unused bits), as for keys and signatures. Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let octet_string = func(bytes: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L198)
  OCTET STRING. Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let utf8_string = func(text: string) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L202)
  UTF8String. Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let printable_string = func(text: string) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L206)
  PrintableString; a character outside its set traps. Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let ia5_string = func(text: string) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L220)
  IA5String; a byte of 128 or more traps. Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let utc_time = func(at: instant.Instant) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L232)
  UTCTime `YYMMDDHHMMSSZ` for an instant in 1950 to 2049 (fractions of a second are dropped). Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let generalized_time = func(at: instant.Instant) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L243)
  GeneralizedTime `YYYYMMDDHHMMSSZ` for an instant in years 0 to 9999. Tagged `#NoDiscard`. Since 0.3.0.
- **DerWriter** · [`export let time = func(at: instant.Instant) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der-writer.nra#L248)
  The RFC 5280 choice: UTCTime through 2049, GeneralizedTime from 2050. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)

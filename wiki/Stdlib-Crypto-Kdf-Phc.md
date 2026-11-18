# `Std.Crypto.Kdf.Phc`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Kdf.Phc`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/phc.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Kdf.Phc`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Phc** · [`export let PhcString = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/phc.nra#L21)
  One parsed or built PHC string. A version of -1 means none; an empty salt or hash means the field is absent (a hash needs a salt). `to_string` gives the canonical encoding, which is also what `equals` and `hash_code` compare: a PHC string is a stored verifier, not a secret. The constructor copies the lists it is given; do not change `salt` or `hash` afterwards. Tagged `#Final`. Since 0.3.0.
- **Phc.PhcString** · [`export last algorithm: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/phc.nra#L23)
  The function identifier, such as "argon2id".
- **Phc.PhcString** · [`export last version: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/phc.nra#L25)
  The `v=` version, or -1.
- **Phc.PhcString** · [`export last salt: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/phc.nra#L29)
  The salt bytes (empty when absent).
- **Phc.PhcString** · [`export last hash: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/phc.nra#L31)
  The hash bytes (empty when absent).
- **Phc.PhcString** · [`export let init = func(id: string, version_number: int, parameter_names: string[list], parameter_values: string[list], salt_bytes: uint8[list], hash_bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/phc.nra#L36)
  Builds a PHC string; parameter names and values are given in order. An invalid identifier, name or value, lists of different lengths, or a hash without a salt traps with "invalid argument: PHC ...".
- **Phc.PhcString** · [`export let parameter = func(name: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/phc.nra#L61)
  The value of the named parameter.
- **Phc.PhcString** · [`export let parameter_names = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/phc.nra#L71)
  The parameter names, in order.
- **Phc.PhcString** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/phc.nra#L79)
- **Phc.PhcString** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/phc.nra#L102)
- **Phc.PhcString** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/phc.nra#L109)
- **Phc** · [`export let parse = func(text: string) -> Result<PhcString>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/phc.nra#L160)
  Parses a PHC string. A malformed string fails with a `Parse` error at the offending field. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#216-stdcryptokdf). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.

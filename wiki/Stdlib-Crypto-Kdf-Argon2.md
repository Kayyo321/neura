# `Std.Crypto.Kdf.Argon2`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Kdf.Argon2`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/argon2.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Kdf.Argon2`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Argon2** · [`export let Argon2Params = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/argon2.nra#L16)
  The cost parameters of an Argon2id hash. Tagged `#Final`, `#Immutable`. Since 0.3.0.
- **Argon2.Argon2Params** · [`export last memory_kib: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/argon2.nra#L18)
  Memory in KiB (m), at least 8 per lane.
- **Argon2.Argon2Params** · [`export last iterations: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/argon2.nra#L20)
  Passes over memory (t), at least 1.
- **Argon2.Argon2Params** · [`export last parallelism: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/argon2.nra#L22)
  Lanes (p), 1 to 2^24 - 1.
- **Argon2.Argon2Params** · [`export last tag_length: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/argon2.nra#L24)
  Tag length in bytes (T), at least 4.
- **Argon2.Argon2Params** · [`export let init = func(memory: int, passes: int, lanes: int, length: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/argon2.nra#L27)
  Stores the parameters; a value out of range traps with "invalid argument: Argon2 <name>".
- **Argon2.Argon2Params** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/argon2.nra#L46)
- **Argon2.Argon2Params** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/argon2.nra#L54)
- **Argon2.Argon2Params** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/argon2.nra#L58)
- **Argon2** · [`export let argon2id = func(password: uint8[list], salt: uint8[list], params: Argon2Params) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/argon2.nra#L64)
  The Argon2id tag of the password with the salt (at least 8 bytes) and parameters. Tagged `#NoDiscard`. Since 0.3.0.
- **Argon2** · [`export let argon2id_with = func(password: uint8[list], salt: uint8[list], secret: uint8[list], associated_data: uint8[list], params: Argon2Params) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/argon2.nra#L69)
  Argon2id with the optional secret value K and associated data X of RFC 9106, section 3.1. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#216-stdcryptokdf). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.

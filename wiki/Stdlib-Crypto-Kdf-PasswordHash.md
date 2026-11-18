# `Std.Crypto.Kdf.PasswordHash`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Kdf.PasswordHash`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/password-hash.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Kdf.PasswordHash`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **PasswordHash** · [`export let hash_password = func(password: uint8[list], salt: uint8[list], params: argon2.Argon2Params) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/password-hash.nra#L20)
  `$argon2id$v=19$m=<m>,t=<t>,p=<p>$<salt>$<hash>` for the password. Tagged `#NoDiscard`. Since 0.3.0.
- **PasswordHash** · [`export let hash_password_scrypt = func(password: uint8[list], salt: uint8[list], log2_n: int, r: int, p: int, length: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/password-hash.nra#L28)
  `$scrypt$ln=<log2 N>,r=<r>,p=<p>$<salt>$<hash>` for the password, with a hash of `length` bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **PasswordHash** · [`export let verify_password = func(password: uint8[list], encoded: string) -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/password-hash.nra#L54)
  Whether the password matches a PHC string of `argon2id` (version 19) or `scrypt`. A malformed string fails with a `Parse` error, and another function or out-of-range parameters with a Custom error of code "crypto.unsupported". Tagged `#NoDiscard`. Since 0.3.0.
- **PasswordHash** · [`export let needs_rehash = func(encoded: string, wanted: argon2.Argon2Params) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/password-hash.nra#L96)
  Whether a stored hash should be recomputed with `wanted` at the next successful login: true when the string does not parse, is not Argon2id version 19, or has any parameter or hash length other than `wanted`'s. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#216-stdcryptokdf). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.

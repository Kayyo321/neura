# `Std.Crypto.Pki.Oid`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.Oid` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/oid.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Oid** · [`export const MaxContentLength: int = 1024`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/oid.nra#L13)
  The longest OID content this library accepts, in bytes. Since 0.3.0.
- **Oid** · [`export let Oid = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/oid.nra#L17)
  An object identifier: an immutable value compared by its DER content octets. Tagged `#Final`. Since 0.3.0.
- **Oid.Oid** · [`export last content: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/oid.nra#L19)
  The DER content octets (no tag or length). Do not change the list.
- **Oid.Oid** · [`export last dotted: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/oid.nra#L21)
  The dotted-decimal form, such as "1.2.840.10045.2.1".
- **Oid.Oid** · [`export let init = func(content_bytes: uint8[list], text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/oid.nra#L24)
  Stores already-validated content and its dotted text; use `of`, `parse` or `from_content`.
- **Oid.Oid** · [`export let matches = func(text: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/oid.nra#L30)
  Whether the dotted form equals the text.
- **Oid.Oid** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/oid.nra#L32)
- **Oid.Oid** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/oid.nra#L39)
- **Oid.Oid** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/oid.nra#L42)
  The dotted-decimal form.
- **Oid** · [`export let from_content = func(bytes: uint8[list], offset: int) -> Result<Oid>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/oid.nra#L52)
  Validates DER content octets and builds the OID. `offset` is the position of the content in the enclosing input, used in error offsets. Empty content, a subidentifier with a leading 0x80 byte (not minimal), a truncated last subidentifier, or content over `MaxContentLength` bytes is a `Parse` error. Tagged `#NoDiscard`. Since 0.3.0.
- **Oid** · [`export let parse = func(text: string) -> Result<Oid>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/oid.nra#L94)
  Parses dotted-decimal text: at least two arcs, the first 0, 1 or 2, the second at most 39 under 0 and 1, decimal arcs without leading zeros. Anything else is a `Parse` error at the offending offset. Tagged `#NoDiscard`. Since 0.3.0.
- **Oid** · [`export let of = func(text: string) -> Oid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/oid.nra#L146)
  The OID with the dotted text; malformed text traps with "invalid argument: OID <text>", because a literal OID in a program is not data. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)

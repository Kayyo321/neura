# `Std.Crypto.Pki.Pem`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.Pem` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/pem.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Pem** · [`export let PemBlock = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/pem.nra#L16)
  One PEM block: a label and the DER bytes. Tagged `#Final`. Since 0.3.0.
- **Pem.PemBlock** · [`export last label: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/pem.nra#L18)
  The label, such as "CERTIFICATE" or "PRIVATE KEY".
- **Pem.PemBlock** · [`export last der: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/pem.nra#L20)
  The decoded bytes.
- **Pem.PemBlock** · [`export let init = func(block_label: string, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/pem.nra#L23)
  A block; a label outside RFC 7468's grammar traps with "invalid argument: PEM label".
- **Pem.PemBlock** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/pem.nra#L31)
- **Pem.PemBlock** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/pem.nra#L47)
- **Pem.PemBlock** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/pem.nra#L56)
  A fixed placeholder for a PEM block that may contain a private key.
- **Pem** · [`export let decode_all = func(text: string) -> Result<PemBlock[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/pem.nra#L85)
  Every block in the text, in order. A BEGIN line without its END line, a mismatched label, or a line inside a block that breaks the strict grammar is a `Parse` error at the offset of the line. Tagged `#NoDiscard`. Since 0.3.0.
- **Pem** · [`export let decode = func(text: string, label: string) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/pem.nra#L186)
  The bytes of the only block with the label. No such block, or more than one, is a `Parse` error. Tagged `#NoDiscard`. Since 0.3.0.
- **Pem** · [`export let encode = func(label: string, der: uint8[list]) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/pem.nra#L204)
  The PEM text of a block: BEGIN line, Base64 in lines of 64, END line, each ending in LF. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)

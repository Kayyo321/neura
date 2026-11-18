# `Std.Crypto.Pki.GeneralName`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.GeneralName` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **GeneralName** · [`export let GeneralNameKind = enum ( OtherName, Rfc822Name, DnsName, X400Address, DirectoryName, EdiPartyName, Uri, IpAddress, RegisteredId )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L17)
  The alternatives, in the order of their context tags [0] to [8]. Since 0.3.0.
- **GeneralName** · [`export let GeneralName = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L26)
  One general name. `text` holds rfc822Name, dNSName and uniformResourceIdentifier (IA5String); `bytes` holds iPAddress (4 or 16 bytes, or 8 or 32 with a mask in name constraints) and the content of the other alternatives; `directory` holds directoryName; `oid` holds registeredID and otherName's type. Tagged `#Final`. Since 0.3.0.
- **GeneralName.GeneralName** · [`export last kind: GeneralNameKind`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L27)
- **GeneralName.GeneralName** · [`export last text: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L28)
- **GeneralName.GeneralName** · [`export last bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L29)
- **GeneralName.GeneralName** · [`export last directory: Optional<names.Name>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L30)
- **GeneralName.GeneralName** · [`export last oid: Optional<Oid>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L31)
- **GeneralName.GeneralName** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L33)
  The DER encoding of the alternative, with its context tag.
- **GeneralName.GeneralName** · [`export let init = func(which: GeneralNameKind, value_text: string, value_bytes: uint8[list], dir: Optional<names.Name>, id: Optional<Oid>, encoded: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L37)
  Records a checked name; `parse` and the `dns`, `ip`, `email`, `uri` and `directory_name` functions build names.
- **GeneralName.GeneralName** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L48)
  Equal when the encodings are equal.
- **GeneralName.GeneralName** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L64)
- **GeneralName.GeneralName** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L68)
  "DNS:<name>", "email:<address>", "URI:<uri>", "IP:<address>[/<mask>]", "DirName:<name>", "RID:<oid>", "othername:<oid>", or "<kind>:<hex>" for the rest.
- **GeneralName** · [`export let ip_text = func(bytes: uint8[list]) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L120)
  Dotted IPv4, RFC 5952 IPv6 text, or either followed by "/" and the mask for 8 or 32 bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **GeneralName** · [`export let parse = func(e: der.Element) -> Result<GeneralName>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L193)
  Reads one GeneralName. The IA5String alternatives must hold only ASCII; an iPAddress must have 4, 8, 16 or 32 bytes; otherName and directoryName must be well formed. Tagged `#NoDiscard`. Since 0.3.0.
- **GeneralName** · [`export let parse_names = func(e: der.Element) -> Result<GeneralName[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L254)
  Reads GeneralNames: a non-empty SEQUENCE of GeneralName. Tagged `#NoDiscard`. Since 0.3.0.
- **GeneralName** · [`export let dns = func(name: string) -> GeneralName`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L289)
  dNSName; a non-ASCII name traps. Tagged `#NoDiscard`. Since 0.3.0.
- **GeneralName** · [`export let email = func(address: string) -> GeneralName`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L293)
  rfc822Name; a non-ASCII address traps. Tagged `#NoDiscard`. Since 0.3.0.
- **GeneralName** · [`export let uri = func(text: string) -> GeneralName`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L297)
  uniformResourceIdentifier; a non-ASCII URI traps. Tagged `#NoDiscard`. Since 0.3.0.
- **GeneralName** · [`export let ip = func(address: uint8[list]) -> GeneralName`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L301)
  iPAddress of 4 or 16 bytes (8 or 32 with a mask); another length traps. Tagged `#NoDiscard`. Since 0.3.0.
- **GeneralName** · [`export let directory_name = func(name: names.Name) -> GeneralName`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/general-name.nra#L305)
  directoryName. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)

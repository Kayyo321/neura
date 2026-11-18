# `Std.Crypto.Pki.Name`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.Name` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Name** · [`export let AttributeTypeAndValue = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L24)
  One attribute type and value. Tagged `#Final`. Since 0.3.0.
- **Name.AttributeTypeAndValue** · [`export last type: Oid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L26)
  The attribute type, such as 2.5.4.3 (commonName).
- **Name.AttributeTypeAndValue** · [`export last value_der: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L28)
  The DER encoding of the value.
- **Name.AttributeTypeAndValue** · [`export last value_tag: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L30)
  The universal tag number of the value.
- **Name.AttributeTypeAndValue** · [`export last text: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L32)
  The value as text when it is a string type, else empty.
- **Name.AttributeTypeAndValue** · [`export last is_text: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L34)
  Whether the value is a string type.
- **Name.AttributeTypeAndValue** · [`export let init = func(attribute: Oid, encoded: uint8[list], tag: int, value_text: string, string_valued: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L39)
  Records a checked pair; `parse` and `of` build pairs.
- **Name.AttributeTypeAndValue** · [`export let matches = func(other: AttributeTypeAndValue) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L49)
  RFC 5280 section 7.1 comparison.
- **Name.AttributeTypeAndValue** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L59)
- **Name.AttributeTypeAndValue** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L62)
- **Name.AttributeTypeAndValue** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L67)
  `<type>=<value>` in RFC 4514 form.
- **Name** · [`export let Name = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L78)
  A distinguished name: RDNs from the most general (such as C) to the most specific (such as CN). Tagged `#Final`. Since 0.3.0.
- **Name.Name** · [`export last rdns: AttributeTypeAndValue[list][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L80)
  The RDNs in encoding order; each holds one or more pairs.
- **Name.Name** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L82)
  The DER encoding of the whole Name.
- **Name.Name** · [`export let init = func(parts: AttributeTypeAndValue[list][list], encoded: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L85)
  Records a checked name; `parse` and `of` build names.
- **Name.Name** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L91)
  Whether the name has no RDNs.
- **Name.Name** · [`export let values_of = func(dotted: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L94)
  The text values of every pair with the attribute type, in order.
- **Name.Name** · [`export let common_names = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L107)
  The commonName values.
- **Name.Name** · [`export let email_addresses = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L110)
  The PKCS #9 emailAddress values (legacy e-mail in the subject).
- **Name.Name** · [`export let starts_with = func(prefix: Name) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L114)
  Whether this name's RDNs begin with all of `prefix`'s RDNs (directoryName subtree match, RFC 5280 section 7.1).
- **Name.Name** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L126)
- **Name.Name** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L134)
- **Name.Name** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L148)
  RFC 4514 text: the most specific RDN first, RDNs joined by ',' and the pairs of one RDN by '+'.
- **Name** · [`export let parse = func(element: der.Element) -> Result<Name>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L254)
  Reads a Name (an RDNSequence) from its element. An empty RDN, a non-SET RDN or a pair that is not SEQUENCE { OBJECT IDENTIFIER, value } is a `Parse` error; a string value must be valid for its type. Tagged `#NoDiscard`. Since 0.3.0.
- **Name** · [`export let parse_der = func(bytes: uint8[list]) -> Result<Name>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L288)
  Parses the DER encoding of a Name. Tagged `#NoDiscard`. Since 0.3.0.
- **Name** · [`export let of = func(types: string[], values: string[]) -> Name`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/name.nra#L295)
  A name of single-valued RDNs in the order given, such as `of(["C", "O", "CN"], ["US", "Example", "host"])` with attribute types given by short name (C, O, OU, L, ST, CN, DC, UID, serialNumber, emailAddress) or dotted OID. Country and serialNumber are PrintableString, emailAddress and DC are IA5String, the others UTF8String. An unknown short name traps. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)

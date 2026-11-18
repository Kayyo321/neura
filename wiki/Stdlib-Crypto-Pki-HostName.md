# `Std.Crypto.Pki.HostName`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.HostName` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/host-name.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **HostName** · [`export let matches_dns = func(certificate: Certificate, host: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/host-name.nra#L21)
  Whether the certificate's subjectAltName has a dNSName matching the host name. A trailing dot on the reference is removed first. Tagged `#NoDiscard`. Since 0.3.0.
- **HostName** · [`export let matches_ip = func(certificate: Certificate, address: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/host-name.nra#L47)
  Whether the certificate's subjectAltName has an iPAddress equal to the address (4 or 16 bytes). Tagged `#NoDiscard`. Since 0.3.0.
- **HostName** · [`export let matches_email = func(certificate: Certificate, address: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/host-name.nra#L64)
  Whether the certificate's subjectAltName has an rfc822Name equal to the address (the local part compared exactly, the domain ignoring ASCII case). Tagged `#NoDiscard`. Since 0.3.0.
- **HostName** · [`export let is_dns_name = func(name: string, allow_wildcard: bool) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/host-name.nra#L137)
  The preferred name syntax: labels of letters, digits and hyphens, not starting or ending with a hyphen, 1 to 63 bytes, the whole at most 253 bytes. With `allow_wildcard`, the first label may be "*". Tagged `#NoDiscard`. Since 0.3.0.
- **HostName** · [`export let parse_ip = func(text: string) -> Optional<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/host-name.nra#L171)
  The 4 or 16 bytes of an IPv4 address in dotted-decimal (four decimal parts of at most 255, no leading zeros) or an IPv6 address in RFC 4291 text (with "::" and an optional dotted IPv4 tail), or None. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)

# `Std.Crypto.Token.Macaroon`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Token.Macaroon` to use this module. The [package specification](Stdlib-Design#2111-stdcryptotoken) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Macaroon** · [`export let WireFormat = enum (V1, V2)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L29)
  The supported binary wire encodings. Since 0.3.0.
- **Macaroon** · [`export let Macaroon = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L34)
  An immutable bearer token. Treat its serialization as a credential. Tagged `#Final`. Since 0.3.0.
- **Macaroon.Macaroon** · [`export let init = func(location: uint8[list], identifier: uint8[list], records: CaveatRecord[list], tag: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L41)
- **Macaroon.Macaroon** · [`export let location = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L52)
  A copy of the unauthenticated location hint. Tagged `#NoDiscard`. Since 0.3.0.
- **Macaroon.Macaroon** · [`export let identifier = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L57)
  A copy of the token identifier. Tagged `#NoDiscard`. Since 0.3.0.
- **Macaroon.Macaroon** · [`export let caveat_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L62)
  The number of attached caveats. Tagged `#NoDiscard`. Since 0.3.0.
- **Macaroon.Macaroon** · [`export let caveat_identifier = func(index: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L68)
  A copy of the caveat identifier, which is the predicate for a first-party caveat. An index outside the caveat list traps with invalid argument. Tagged `#NoDiscard`. Since 0.3.0.
- **Macaroon.Macaroon** · [`export let caveat_location = func(index: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L74)
  A copy of the third-party location hint, empty when absent. An index outside the caveat list traps with invalid argument. Tagged `#NoDiscard`. Since 0.3.0.
- **Macaroon.Macaroon** · [`export let needs_discharge = func(index: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L80)
  Whether the caveat must be discharged before use. An index outside the caveat list traps with invalid argument. Tagged `#NoDiscard`. Since 0.3.0.
- **Macaroon.Macaroon** · [`export let add_first_party = func(predicate: uint8[list]) -> Macaroon`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L86)
  Attenuates the token with one first-party predicate. The source token is unchanged. An empty predicate, an oversized field or too many caveats traps with invalid argument. Tagged `#NoDiscard`. Since 0.3.0.
- **Macaroon.Macaroon** · [`export let add_third_party = func(location: uint8[list], identifier: uint8[list], caveat_key: uint8[list]) -> Result<Macaroon>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L95)
  Attenuates with a third-party caveat and a fresh 24-byte OS nonce. Entropy failures are returned. Choose an unpredictable caveat key and pass it directly to the third party by a secure channel. Tagged `#NoDiscard`. Since 0.3.0.
- **Macaroon.Macaroon** · [`export let add_third_party_with_nonce = func(location: uint8[list], identifier: uint8[list], caveat_key: uint8[list], nonce: uint8[list]) -> Macaroon`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L106)
  Attenuates with one third-party caveat using a caller-supplied 24-byte nonce. Use a fresh nonce every time. An invalid length or too many caveats traps with invalid argument. Tagged `#NoDiscard`. Since 0.3.0.
- **Macaroon.Macaroon** · [`export let bind = func(discharge: Macaroon) -> Macaroon`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L136)
  Binds a discharge to this root token. Bind only after adding every caveat; bind all discharges, including nested ones, to the same final root. Tagged `#NoDiscard`. Since 0.3.0.
- **Macaroon.Macaroon** · [`export let verify = func(root_key: uint8[list], discharges: Macaroon[list], check: func(uint8[list]) -> bool) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L145)
  Verifies the root signature and every bound discharge, then requires the supplied checker to accept every authenticated first-party predicate. A missing discharge, multiple discharges for one identifier or a cyclic discharge chain fails closed. The bounds are 256 supplied discharges, depth 32 and 4096 visited caveats. Tagged `#NoDiscard`. Since 0.3.0.
- **Macaroon.Macaroon** · [`export let serialize = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L169)
  The compact v2 binary wire format as unpadded base64url. Pass a format explicitly to serialize as libmacaroons v1. Tagged `#NoDiscard`. Since 0.3.0.
- **Macaroon.Macaroon** · [`export let serialize_binary = func(format: WireFormat) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L175)
  The binary v1 or v2 wire bytes before base64url encoding. Tagged `#NoDiscard`. Since 0.3.0.
- **Macaroon.Macaroon** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L185)
  Whether the tokens have the same wire fields and signature. Since 0.3.0.
- **Macaroon.Macaroon** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L196)
  The hash of the canonical v2 encoding. Since 0.3.0.
- **Macaroon.Macaroon** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L201)
  A fixed placeholder for the bearer credential. Since 0.3.0.
- **Macaroon** · [`export let create = func(location: uint8[list], root_key: uint8[list], identifier: uint8[list]) -> Macaroon`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L302)
  Issues a root macaroon or a discharge with the given identifier and root key. The root key and identifier must be nonempty and each wire field must be at most 32767 bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Macaroon** · [`export let parse = func(token: string) -> Result<Macaroon>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L318)
  Parses base64url binary v1 or v2. Decode does not authorize the token. Invalid or noncanonical framing returns crypto.token. Tagged `#NoDiscard`. Since 0.3.0.
- **Macaroon** · [`export let parse_binary = func(bytes: uint8[list]) -> Result<Macaroon>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/macaroon.nra#L324)
  Parses binary v1 or v2 wire bytes. Malformed input returns crypto.token. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)

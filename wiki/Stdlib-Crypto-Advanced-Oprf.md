# `Std.Crypto.Advanced.Oprf`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Advanced.Oprf` to use this module. The [package specification](Stdlib-Design#2113-stdcryptoadvanced) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Oprf** · [`export let Mode = enum (Base, Verifiable, Partial)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L24)
  The base, verifiable and partially oblivious modes, in RFC 9497 order. Since 0.3.0.
- **Oprf** · [`export let Suite = groups.Suite`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L30)
  The RFC 9497 ciphersuites implemented by this module. The overloads without a suite use Ristretto255Sha512. Since 0.3.0.
- **Oprf** · [`export let Evaluation = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L230)
  The ordered evaluated elements and their encoded DLEQ proof. The proof is empty in base mode. Tagged `#Final`. Since 0.3.0.
- **Oprf.Evaluation** · [`export let init = func(evaluated: uint8[list][list], encoded_proof: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L235)
- **Oprf.Evaluation** · [`export let evaluated_element = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L243)
  The canonical evaluated element of a single-element response. A batch with multiple elements traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.Evaluation** · [`export let evaluated_elements = func -> uint8[list][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L253)
  The canonical evaluated elements in request order. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.Evaluation** · [`export let encoded_proof = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L258)
  Two canonical scalars in the selected ciphersuite, or an empty list in base mode. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.Evaluation** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L263)
  Compares the ordered evaluated elements and proof by value. Since 0.3.0.
- **Oprf.Evaluation** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L281)
  Hashes the ordered evaluated elements and proof. Since 0.3.0.
- **Oprf.Evaluation** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L297)
  The response type without the wire data. Since 0.3.0.
- **Oprf** · [`export let BlindState = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L303)
  One client exchange. The state owns each input and blind until finalization or discard. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.0.
- **Oprf.BlindState** · [`export let init = func(curve: groups.Group, selected: Mode, private_inputs, nonces: uint8[list][list], metadata: uint8[list], blinded: class [list], verifier_key: class)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L313)
- **Oprf.BlindState** · [`export let blinded_element = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L331)
  The canonical single-element request. A batch with multiple elements traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.BlindState** · [`export let blinded_elements = func -> uint8[list][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L341)
  The canonical blinded elements in input order. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.BlindState** · [`export let discard = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L352)
  Overwrites the retained inputs and blinds. A discarded exchange cannot be finalized. Since 0.3.0.
- **Oprf.BlindState** · [`export let is_spent = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L362)
  Whether finalization consumed the exchange or the caller discarded it. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.BlindState** · [`export let finalize = func(evaluated_element, proof: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L368)
  Checks the single response and returns the PRF output. Each attempt consumes this state. A spent state traps. Output size is the ciphersuite hash size. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.BlindState** · [`export let finalize_batch = func(evaluated_elements: uint8[list][list], proof: uint8[list]) -> Result<uint8[list][list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L378)
  Checks the entire response before returning the outputs in original input order. Each attempt consumes every blind, including attempts with malformed or unauthenticated responses. A spent state traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.BlindState** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L413)
  A fixed placeholder for the private exchange. Since 0.3.0.
- **Oprf** · [`export let Client = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L429)
  Configures the mode and an authenticated server public key for verifiable modes. Tagged `#Final`. Since 0.3.0.
- **Oprf.Client** · [`export let init = func(selected: Mode, server_key: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L438)
  In base mode, pass an empty public key. In other modes, pass the server's canonical non-identity key. An invalid configured key traps. Since 0.3.0.
- **Oprf.Client** · [`export let blind_with = func(input, info, nonce: uint8[list]) -> Result<BlindState>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L452)
  Blinds a private input using a supplied canonical nonzero scalar for a deterministic run. Use empty info outside partially oblivious mode. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.Client** · [`export let blind_batch_with = func(inputs: uint8[list][list], info: uint8[list], nonces: uint8[list][list]) -> Result<BlindState>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L493)
  Blinds 1 to 65536 inputs in order, using one caller-supplied canonical nonzero blind per item. Use independently generated fresh blinds. Provide empty info outside partially oblivious mode. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.Client** · [`export let blind = func(input, info: uint8[list]) -> Result<BlindState>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L516)
  Blinds a private input using OS entropy or a caller's DRBG. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.Client** · [`export let blind_batch = func(inputs: uint8[list][list], info: uint8[list]) -> Result<BlindState>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L521)
  Blinds the inputs with independent scalars from OS entropy or a caller's DRBG. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.Client** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L527)
  The client type without a printed key or transcript. Since 0.3.0.
- **Oprf** · [`export let Server = class implements PrivateKey<Server>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L551)
  A private server key for one RFC 9497 mode. Tagged `#Final`. Since 0.3.0.
- **Oprf.Server** · [`export let init = func(selected: Mode, scalar: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L560)
  Imports a canonical nonzero server scalar for the selected mode and ciphersuite. Since 0.3.0.
- **Oprf.Server** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L581)
  A copy of the secret server scalar. Since 0.3.0.
- **Oprf.Server** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L586)
  Overwrites the retained server scalar. Since 0.3.0.
- **Oprf.Server** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L591)
  Whether wipe was called. Since 0.3.0.
- **Oprf.Server** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L596)
  Requests page locking through SecretBytes. Since 0.3.0.
- **Oprf.Server** · [`export let public_key_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L601)
  The canonical public key for configuring a verifiable client. Since 0.3.0.
- **Oprf.Server** · [`export let evaluate_with = func(encoded, info, proof_nonce: uint8[list]) -> Result<Evaluation>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L657)
  Evaluates one blinded element with an explicit fresh proof nonce. In base mode pass an empty nonce and info. Reusing a proof nonce can disclose the server key. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.Server** · [`export let evaluate_batch_with = func(encoded: uint8[list][list], info, proof_nonce: uint8[list]) -> Result<Evaluation>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L663)
  Evaluates 1 to 65536 ordered blinded elements with a single fresh proof nonce. In base mode pass an empty proof nonce and info. Reusing a proof nonce can disclose the server key. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.Server** · [`export let evaluate = func(encoded, info: uint8[list]) -> Result<Evaluation>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L683)
  Evaluates under the server key, using OS entropy or a caller's DRBG when a proof is needed. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.Server** · [`export let evaluate_batch = func(encoded: uint8[list][list], info: uint8[list]) -> Result<Evaluation>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L689)
  Evaluates the ordered batch and produces one proof outside base mode. The proof nonce comes from OS entropy or a caller's DRBG. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.Server** · [`export let full_evaluate = func(input, info: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L696)
  Computes the PRF result for a known input without running the blinded protocol. Output size is the ciphersuite hash size. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf.Server** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L718)
  A fixed placeholder for the private server key. Since 0.3.0.
- **Oprf** · [`export let derive_key_pair = func(mode: Mode, seed, info: uint8[list]) -> Result<Server>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L725)
  RFC 9497 DeriveKeyPair with a 32-byte seed and separate key-derivation info. The info is bound to the derived key and is independent of POPRF evaluation info. Tagged `#NoDiscard`. Since 0.3.0.
- **Oprf** · [`export let generate_key_pair = func(mode: Mode) -> Result<Server>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/oprf.nra#L757)
  Derives a server key from a 32-byte seed from OS entropy or a caller's DRBG. Entropy failures propagate. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)

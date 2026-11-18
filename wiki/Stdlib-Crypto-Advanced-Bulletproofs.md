# `Std.Crypto.Advanced.Bulletproofs`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Advanced.Bulletproofs` to use this module. The [package specification](Stdlib-Design#2113-stdcryptoadvanced) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/bulletproofs.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Bulletproofs** · [`export let prove_with = func(transcript: merlin.Transcript, bits: int, value: uint64, blinding: uint8[list], random: uint8[list]) -> Result<[uint8[list], uint8[list]]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/bulletproofs.nra#L483)
  Proves one value in [0, 2^bits) and returns [commitment, proof] in Dalek's encoding. Supply one canonical 32-byte blinding scalar and 64 * (2 * bits + 4) independent unpredictable bytes. See prove_aggregate_with for the supported widths and random material requirements. Tagged `#NoDiscard`. Since 0.3.0.
- **Bulletproofs** · [`export let prove = func(transcript: merlin.Transcript, bits: int, value: uint64, blinding: uint8[list]) -> Result<[uint8[list], uint8[list]]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/bulletproofs.nra#L492)
  Proves one value using fresh operating system randomness for the proof. Tagged `#NoDiscard`. Since 0.3.0.
- **Bulletproofs** · [`export let verify = func(transcript: merlin.Transcript, bits: int, commitment: uint8[list], proof: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/bulletproofs.nra#L501)
  Verifies one Dalek range proof deterministically by checking both group equations. Tagged `#NoDiscard`. Since 0.3.0.
- **Bulletproofs** · [`export let prove_aggregate_with = func(transcript: merlin.Transcript, bits: int, values: uint64[list], blindings: uint8[list][list], random: uint8[list]) -> Result<[uint8[list][list], uint8[list]]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/bulletproofs.nra#L516)
  Produces one Dalek range proof for an ordered list of values in [0, 2^bits). Returns the list of commitments in input order and then the proof bytes. Widths are 8, 16, 32, and 64. The list contains 1, 2, 4, 8, or 16 values and one canonical 32-byte blinding scalar per value. Uniform independent blinding scalars hide the committed values. Supply 64 * value_count * (2 * bits + 4) independent unpredictable bytes for production proofs. A bad width, value or blinding count, or random byte count traps. Noncanonical blindings, out-of-range values, and degenerate proof points return errors. The caller's Merlin transcript is forked. Scalar multiplication of secret values is branch-free in source. The runtime does not guarantee constant time. Tagged `#NoDiscard`. Since 0.3.0.
- **Bulletproofs** · [`export let prove_aggregate = func(transcript: merlin.Transcript, bits: int, values: uint64[list], blindings: uint8[list][list]) -> Result<[uint8[list][list], uint8[list]]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/bulletproofs.nra#L541)
  Produces an aggregate range proof with fresh operating system randomness. The values and caller-supplied blindings follow the same contract as prove_aggregate_with. Tagged `#NoDiscard`. Since 0.3.0.
- **Bulletproofs** · [`export let verify_aggregate = func(transcript: merlin.Transcript, bits: int, commitments: uint8[list][list], proof: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/bulletproofs.nra#L557)
  Verifies a Dalek aggregate range proof deterministically against the ordered commitment list. Widths are 8, 16, 32, and 64. A list contains 1, 2, 4, 8, or 16 commitments in proving order. An unsupported width traps. Wrong encodings, mismatched transcripts, invalid counts, and failed group equations return false. The caller's Merlin transcript is forked. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)

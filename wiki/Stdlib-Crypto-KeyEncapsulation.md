# `Std.Crypto.KeyEncapsulation`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.KeyEncapsulation`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/key-encapsulation.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.KeyEncapsulation`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **KeyEncapsulation** · [`export let Encapsulation = class<SharedSecret, Ciphertext>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/key-encapsulation.nra#L11)
  The result of `encapsulate`: the shared secret to keep and the ciphertext to send. Tagged `#Final`. Since 0.3.0.
- **KeyEncapsulation.Encapsulation** · [`export last shared_secret: SharedSecret`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/key-encapsulation.nra#L13)
  The shared secret.
- **KeyEncapsulation.Encapsulation** · [`export last ciphertext: Ciphertext`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/key-encapsulation.nra#L15)
  The ciphertext for the holder of the private key.
- **KeyEncapsulation.Encapsulation** · [`export let init = func(secret: SharedSecret, encapsulated: Ciphertext)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/key-encapsulation.nra#L18)
  Stores both parts.
- **KeyEncapsulation.Encapsulation** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/key-encapsulation.nra#L24)
  A fixed text: the shared secret is never printed.
- **KeyEncapsulation** · [`export let KeyEncapsulation = interface<PubKey, PrivKey, SharedSecret, Ciphertext>:CryptographicPrimitive<Ciphertext>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/key-encapsulation.nra#L30)
  Establishes a shared secret for the holder of a key pair, classically or post-quantum. The primitive is named by `Ciphertext`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#201-contracts). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.

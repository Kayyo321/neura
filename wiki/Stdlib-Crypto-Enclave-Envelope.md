# `Std.Crypto.Enclave.Envelope`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Enclave.Envelope` to use this module. The [package specification](Stdlib-Design#2112-stdcryptoenclave) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/envelope.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Envelope** · [`export let EnvelopeCrypter = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/envelope.nra#L35)
  Encrypts and decrypts messages with associated data through a configured key store. Since 0.3.0.
- **Envelope** · [`export let Envelope = class<Handle> implements EnvelopeCrypter`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/envelope.nra#L89)
  An envelope engine bound to one key label and one task. Uses a single encryption DEK cache. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.0.
- **Envelope.Envelope** · [`export let init = func(provider: VersionedKeyStore<Handle>, key_label: string, version: uint32, uses_per_dek: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/envelope.nra#L101)
  Sets the initial KEK version and DEK use limit (1 to 4096). A zero key version, invalid label (outside 1 to 255 UTF-8 bytes) or invalid limit traps. Since 0.3.0.
- **Envelope.Envelope** · [`export let rotate_kek = func(version: uint32) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/envelope.nra#L119)
  Starts using a newer live version of this label and wipes the cached DEK. An error leaves the current encryption version in place. Older versions can still decrypt while live in the provider. Tagged `#NoDiscard`. Since 0.3.0.
- **Envelope.Envelope** · [`export let forget_dek = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/envelope.nra#L132)
  Wipes the cache's DEK and discards the current nonce sequence. The next seal needs a fresh DEK. Since 0.3.0.
- **Envelope.Envelope** · [`export let seal = func(plaintext: uint8[list], associated_data: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/envelope.nra#L146)
  Seals under the current live KEK, using the OS to generate a DEK and nonce prefix on a cache miss. Returns provider or entropy errors. A message or associated data over 1 MiB traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Envelope.Envelope** · [`export let seal_with_drbg = func(plaintext: uint8[list], associated_data: uint8[list], generator: drbg.ChaCha20Drbg) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/envelope.nra#L152)
  Seals with a caller-supplied generator for a fresh DEK and nonce prefix on a cache miss. Tagged `#NoDiscard`. Since 0.3.0.
- **Envelope.Envelope** · [`export let seal_with_material = func(plaintext: uint8[list], associated_data: uint8[list], material: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/envelope.nra#L161)
  Deterministic DEK generation for tests. The 40 bytes are DEK || nonce prefix and are used only on a cache miss. Other lengths trap. Each epoch under a repeated DEK needs a unique prefix. Tagged `#NoDiscard`. Since 0.3.0.
- **Envelope.Envelope** · [`export let open = func(envelope: uint8[list], associated_data: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/envelope.nra#L174)
  Opens under the KEK version stated in the header. Wrong AAD, a missing key or failed authentication returns crypto.authentication. A malformed header returns crypto.envelope. Tagged `#NoDiscard`. Since 0.3.0.
- **Envelope.Envelope** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/envelope.nra#L276)
  A fixed placeholder that hides key and cache details. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)

# `Std.Encoding.JsonCodecSupport`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [JSON](Stdlib-Encoding-Json)

Import this module as `Std.Encoding.JsonCodecSupport` for the primitive conversions used by codecs derived from `Json.Codec<T>`.

## Detailed contracts

- `parse` accepts at most 16,777,216 UTF-8 bytes. It preserves object member order and every occurrence of repeated names so the codec can apply its schema. Syntax, offsets, and the nesting limit follow `Json.parse`. An oversized input returns `json.codec.too_large` at `$` before parsing.
- The integer decoders require JSON number text in integer form and check the destination type's bounds. The encoders return exact decimal number text, including `uint64` values above the signed range.
- The float decoders round once to the destination format, ties to even. The encoders use that format's shortest round-trip text. Encoding NaN or an infinity and decoding a number that rounds to an infinity return `json.codec.not_finite`.
- Primitive conversions return `json.codec.type` if the JSON kind differs from the expected kind. A malformed numeric lexeme or an integer outside the required form or bounds returns `json.codec.range`. Each custom error message begins with the supplied path.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **JsonCodecSupport** · [`export let parse = func(text: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L24)
  Parses codec input with a 16 MiB bound and retains repeated member names in order. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let encode_bool = func(value: bool, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L57)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let decode_bool = func(value: json.JsonValue, path: string) -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L60)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let encode_string = func(value: string, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L66)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let decode_string = func(value: json.JsonValue, path: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L69)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let encode_int8 = func(value: int8, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L75)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let decode_int8 = func(value: json.JsonValue, path: string) -> Result<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L78)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let encode_int16 = func(value: int16, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L84)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let decode_int16 = func(value: json.JsonValue, path: string) -> Result<int16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L87)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let encode_int32 = func(value: int32, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L93)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let decode_int32 = func(value: json.JsonValue, path: string) -> Result<int32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L96)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let encode_int64 = func(value: int64, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L102)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let decode_int64 = func(value: json.JsonValue, path: string) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L105)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let encode_uint8 = func(value: uint8, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L111)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let decode_uint8 = func(value: json.JsonValue, path: string) -> Result<uint8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L114)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let encode_uint16 = func(value: uint16, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L120)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let decode_uint16 = func(value: json.JsonValue, path: string) -> Result<uint16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L123)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let encode_uint32 = func(value: uint32, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L129)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let decode_uint32 = func(value: json.JsonValue, path: string) -> Result<uint32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L132)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let encode_uint64 = func(value: uint64, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L138)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let decode_uint64 = func(value: json.JsonValue, path: string) -> Result<uint64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L141)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let encode_float16 = func(value: float16, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L147)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let decode_float16 = func(value: json.JsonValue, path: string) -> Result<float16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L155)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let encode_bfloat16 = func(value: bfloat16, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L165)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let decode_bfloat16 = func(value: json.JsonValue, path: string) -> Result<bfloat16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L173)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let encode_float32 = func(value: float32, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L183)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let decode_float32 = func(value: json.JsonValue, path: string) -> Result<float32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L191)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let encode_float64 = func(value: float64, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L201)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **JsonCodecSupport** · [`export let decode_float64 = func(value: json.JsonValue, path: string) -> Result<float64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L209)
  Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

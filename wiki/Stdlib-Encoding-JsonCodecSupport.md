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
- **JsonCodecSupport** · [`export let encode_bool = func(value: bool, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L59)
- **JsonCodecSupport** · [`export let decode_bool = func(value: json.JsonValue, path: string) -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L64)
- **JsonCodecSupport** · [`export let encode_string = func(value: string, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L72)
- **JsonCodecSupport** · [`export let decode_string = func(value: json.JsonValue, path: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L77)
- **JsonCodecSupport** · [`export let encode_int8 = func(value: int8, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L85)
- **JsonCodecSupport** · [`export let decode_int8 = func(value: json.JsonValue, path: string) -> Result<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L90)
- **JsonCodecSupport** · [`export let encode_int16 = func(value: int16, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L98)
- **JsonCodecSupport** · [`export let decode_int16 = func(value: json.JsonValue, path: string) -> Result<int16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L103)
- **JsonCodecSupport** · [`export let encode_int32 = func(value: int32, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L111)
- **JsonCodecSupport** · [`export let decode_int32 = func(value: json.JsonValue, path: string) -> Result<int32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L116)
- **JsonCodecSupport** · [`export let encode_int64 = func(value: int64, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L124)
- **JsonCodecSupport** · [`export let decode_int64 = func(value: json.JsonValue, path: string) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L129)
- **JsonCodecSupport** · [`export let encode_uint8 = func(value: uint8, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L137)
- **JsonCodecSupport** · [`export let decode_uint8 = func(value: json.JsonValue, path: string) -> Result<uint8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L142)
- **JsonCodecSupport** · [`export let encode_uint16 = func(value: uint16, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L150)
- **JsonCodecSupport** · [`export let decode_uint16 = func(value: json.JsonValue, path: string) -> Result<uint16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L155)
- **JsonCodecSupport** · [`export let encode_uint32 = func(value: uint32, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L163)
- **JsonCodecSupport** · [`export let decode_uint32 = func(value: json.JsonValue, path: string) -> Result<uint32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L168)
- **JsonCodecSupport** · [`export let encode_uint64 = func(value: uint64, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L176)
- **JsonCodecSupport** · [`export let decode_uint64 = func(value: json.JsonValue, path: string) -> Result<uint64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L181)
- **JsonCodecSupport** · [`export let encode_float16 = func(value: float16, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L189)
- **JsonCodecSupport** · [`export let decode_float16 = func(value: json.JsonValue, path: string) -> Result<float16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L199)
- **JsonCodecSupport** · [`export let encode_bfloat16 = func(value: bfloat16, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L211)
- **JsonCodecSupport** · [`export let decode_bfloat16 = func(value: json.JsonValue, path: string) -> Result<bfloat16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L221)
- **JsonCodecSupport** · [`export let encode_float32 = func(value: float32, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L233)
- **JsonCodecSupport** · [`export let decode_float32 = func(value: json.JsonValue, path: string) -> Result<float32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L243)
- **JsonCodecSupport** · [`export let encode_float64 = func(value: float64, path: string) -> Result<json.JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L255)
- **JsonCodecSupport** · [`export let decode_float64 = func(value: json.JsonValue, path: string) -> Result<float64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json-codec-support.nra#L265)
<!-- END SOURCE API -->

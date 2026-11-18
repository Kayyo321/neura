# `Std.Reflect.Codec.JsonBridge`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Codec.JsonBridge`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/json-bridge.nra) defines the shipped declarations: a reflection-driven JSON serializer. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. [Section 20 of the standard library specification](Stdlib-Design#20-stdreflect) describes `Std.Reflect` as a whole.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **JsonBridge** · [`export const Redacted: string = "<redacted>"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/json-bridge.nra#L40)
  The placeholder written for a `#Sensitive` field. Since 0.3.0.
- **JsonBridge** · [`export let to_json = func(value: model.Variant) -> JsonValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/json-bridge.nra#L44)
  The value as a JSON tree. A NaN or infinite float and a reference cycle trap. Since 0.3.0.
- **JsonBridge** · [`export let encode = func(value: model.Variant) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/json-bridge.nra#L51)
  The value as JSON text. Since 0.3.0.
- **JsonBridge** · [`export let from_json = func(value: JsonValue, type: model.TypeDescriptor) -> Result<model.Variant>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/json-bridge.nra#L57)
  The value of type `type` that a JSON tree describes. A mismatch returns `Error.custom` with the code `reflect.json.type`, `reflect.json.missing`, `reflect.json.range` or `reflect.json.variant` and a message that begins with the JSON path. Since 0.3.0.
- **JsonBridge** · [`export let decode = func(text: string, type: model.TypeDescriptor) -> Result<model.Variant>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/json-bridge.nra#L62)
  `json.parse`, then `from_json`. Since 0.3.0.
- **JsonBridge** · [`export let decode_as = func<T>(text: string) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/json-bridge.nra#L69)
  `decode` for the type `T`, unpacked: a JSON text that does not describe a `T` traps. Since 0.3.0.
<!-- END SOURCE API -->

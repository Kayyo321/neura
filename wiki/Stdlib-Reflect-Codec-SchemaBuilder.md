# `Std.Reflect.Codec.SchemaBuilder`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Codec.SchemaBuilder`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/schema-builder.nra) defines the shipped declarations: JSON Schemas from descriptors. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. [Section 20 of the standard library specification](Stdlib-Design#22-stdreflect) describes `Std.Reflect` as a whole.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **SchemaBuilder** · [`export let json_schema = func(type: model.TypeDescriptor) -> JsonValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/schema-builder.nra#L15)
  The JSON Schema of `type` as a JSON tree. Since 0.3.0.
- **SchemaBuilder** · [`export let json_schema_text = func(type: model.TypeDescriptor) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/schema-builder.nra#L36)
  The JSON Schema of `type` as text. Since 0.3.0.
- **SchemaBuilder** · [`export let protobuf_schema = func(type: model.TypeDescriptor) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/schema-builder.nra#L44)
  The proto3 messages for a class or record and every class, record and enum it reaches. Fields are numbered from 1 in declaration order and `#Skip` fields are left out. Mapping: `bool`, `string`, `int32`, `int64`, `uint32`, `uint64`, `float` (float32 and narrower floats), `double`; int8 and int16 are `int32`, uint8 and uint16 `uint32`; a list is `repeated` (a list of lists traps); `Optional<T>` is `optional T`; a fieldless enum is a proto enum; an enum with payloads, a tuple or a function value traps. Since 0.3.0.
- **SchemaBuilder** · [`export let sql_table = func(type: model.TypeDescriptor, table: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/schema-builder.nra#L60)
  A `CREATE TABLE` statement for a class or record whose fields are scalars, strings or `Optional` of them: a column per field (`#Skip` left out), `NOT NULL` unless optional. Types: `BOOLEAN`, `SMALLINT` (8- and 16-bit), `INTEGER` (32-bit), `BIGINT` (64-bit; uint64 values above the BIGINT range do not fit), `REAL`, `DOUBLE PRECISION`, `TEXT`. Another field type traps. Since 0.3.0.
<!-- END SOURCE API -->

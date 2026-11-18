# `Std.Reflect.Attribute.Attributes`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Attribute.Attributes`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/attribute/attributes.nra) defines the shipped declarations: tags as runtime attributes. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. [Section 20 of the standard library specification](Stdlib-Design#20-stdreflect) describes `Std.Reflect` as a whole.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Attributes** · [`export let Attribute = model.Attribute`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/attribute/attributes.nra#L12)
  A tag and its arguments. Since 0.3.0.
- **Attributes** · [`export let AttributeTarget = enum (Type, Field, Variant, Method, Parameter)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/attribute/attributes.nra#L16)
  The kind of declaration an attribute was written on. Since 0.3.0.
- **Attributes** · [`export let of_type = func(type: model.TypeDescriptor) -> Attribute[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/attribute/attributes.nra#L20)
  The tags of a type. Since 0.3.0.
- **Attributes** · [`export let of_field = func(field: model.FieldInfo) -> Attribute[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/attribute/attributes.nra#L24)
  The tags of a field. Since 0.3.0.
- **Attributes** · [`export let find = func(items: Attribute[list], tag: string) -> Attribute`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/attribute/attributes.nra#L28)
  The first attribute named `tag` among `items`, or nil. Since 0.3.0.
- **Attributes** · [`export let has = func(items: Attribute[list], tag: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/attribute/attributes.nra#L32)
  Whether `items` has an attribute named `tag`. Since 0.3.0.
- **Attributes** · [`export let serial_name = func(field: model.FieldInfo) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/attribute/attributes.nra#L36)
  The wire name of a field: its `#SerialName` argument, or its declared name. Since 0.3.0.
- **Attributes** · [`export let skipped = func(field: model.FieldInfo) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/attribute/attributes.nra#L49)
  Whether a field is tagged `#Skip`: serializers leave it out. Since 0.3.0.
- **Attributes** · [`export let sensitive = func(field: model.FieldInfo) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/attribute/attributes.nra#L53)
  Whether a field is tagged `#Sensitive`: serializers write a placeholder instead of its value. Since 0.3.0.
<!-- END SOURCE API -->

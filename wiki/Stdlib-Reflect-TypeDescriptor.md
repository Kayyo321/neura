# `Std.Reflect.Type.TypeDescriptor`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Type.TypeDescriptor`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/type/type-descriptor.nra) defines the shipped declarations: type descriptors. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. [Section 20 of the standard library specification](Stdlib-Design#20-stdreflect) describes `Std.Reflect` as a whole.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TypeDescriptor** · [`export let TypeDescriptor = model.TypeDescriptor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/type/type-descriptor.nra#L11)
  The descriptor of one type of the program's reflection universe. Since 0.3.0.
- **TypeDescriptor** · [`export let of = func<T>() -> TypeDescriptor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/type/type-descriptor.nra#L15)
  The descriptor of `T`. Since 0.3.0.
- **TypeDescriptor** · [`export let of_value = func<T>(value: T) -> TypeDescriptor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/type/type-descriptor.nra#L20)
  The descriptor of the value's type: the dynamic class of a reference, the static type otherwise. A nil reference gives the nil descriptor (ordinal 0). Since 0.3.0.
- **TypeDescriptor** · [`export let of_ordinal = func(ordinal: int) -> TypeDescriptor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/type/type-descriptor.nra#L24)
  The descriptor at `ordinal` (see TypeDescriptor.ordinal, FieldInfo.type_ordinal). Since 0.3.0.
- **TypeDescriptor** · [`export let of_info = func(info: type_info.TypeInfo) -> TypeDescriptor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/type/type-descriptor.nra#L29)
  The descriptor whose `typeof` record equals `info`, or nil when the program does not reflect that type. Since 0.3.0.
<!-- END SOURCE API -->

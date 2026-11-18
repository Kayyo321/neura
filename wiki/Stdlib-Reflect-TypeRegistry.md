# `Std.Reflect.Core.TypeRegistry`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Core.TypeRegistry`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/type-registry.nra) defines the shipped declarations: lookup of every reflected type. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. [Section 20 of the standard library specification](Stdlib-Design#20-stdreflect) describes `Std.Reflect` as a whole.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TypeRegistry** · [`export let count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/type-registry.nra#L12)
  The number of types in the universe; ordinals run from 0 (nil) to `count() - 1`. Since 0.3.0.
- **TypeRegistry** · [`export let at = func(ordinal: int) -> model.TypeDescriptor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/type-registry.nra#L16)
  The descriptor at `ordinal`. Since 0.3.0.
- **TypeRegistry** · [`export let find_id = func(id: type_ids.TypeId) -> model.TypeDescriptor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/type-registry.nra#L20)
  The descriptor with this fingerprint, or nil. Since 0.3.0.
- **TypeRegistry** · [`export let find_name = func(qualified_name: string) -> model.TypeDescriptor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/type-registry.nra#L33)
  The first descriptor with this qualified name, or nil. Two files can declare one qualified name; `find_id` tells them apart. Since 0.3.0.
<!-- END SOURCE API -->

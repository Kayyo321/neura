# `Std.Reflect.Core.Reflectable`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Core.Reflectable`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/reflectable.nra) defines the shipped declarations: the root contract of reflection as operations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. [Section 20 of the standard library specification](Stdlib-Design#22-stdreflect) describes `Std.Reflect` as a whole.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Reflectable** · [`export let descriptor = func<T>() -> model.TypeDescriptor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/reflectable.nra#L13)
  The static descriptor of `T`. Since 0.3.0.
- **Reflectable** · [`export let descriptor_of = func<T>(value: T) -> model.TypeDescriptor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/reflectable.nra#L17)
  The descriptor of the value's type: its dynamic class for a reference. Since 0.3.0.
- **Reflectable** · [`export let cursor = func<T>(value: T) -> cursors.ValueCursor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/reflectable.nra#L21)
  A read-only cursor at the root of the value, bound to its descriptor. Since 0.3.0.
- **Reflectable** · [`export let cursor_mut = func<T>(value: T, steps: int[list]) -> cursors.ValueCursorMut`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/reflectable.nra#L25)
  A writable cursor at the field path `steps` of the value. Since 0.3.0.
<!-- END SOURCE API -->

# `Std.Reflect.Dynamic.Variant`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Dynamic.Variant`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/dynamic/variant.nra) defines the shipped declarations: checked dynamic values. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. [Section 20 of the standard library specification](Stdlib-Design#20-stdreflect) describes `Std.Reflect` as a whole.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Variant** · [`export let Variant = model.Variant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/dynamic/variant.nra#L11)
  A value with its type. Since 0.3.0.
- **Variant** · [`export let of = func<T>(value: T) -> Variant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/dynamic/variant.nra#L15)
  The value with its type: the dynamic class of a reference, the static type otherwise. Since 0.3.0.
- **Variant** · [`export let construct = func(type: model.TypeDescriptor, selector: int, arguments: Variant[list]) -> Variant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/dynamic/variant.nra#L20)
  A new value of `type`: constructor `selector` of a class or record, variant `selector` of an enum, or an array, list or tuple of the arguments (selector 0). Each argument is checked against its parameter. Since 0.3.0.
<!-- END SOURCE API -->

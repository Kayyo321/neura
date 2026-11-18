# `Std.Reflect.Core.ReflectPolicy`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Core.ReflectPolicy`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/reflect-policy.nra) defines the shipped declarations: the markers that choose what reflection keeps of a class. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. [Section 20 of the standard library specification](Stdlib-Design#20-stdreflect) describes `Std.Reflect` as a whole.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ReflectPolicy** · [`export let Strip = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/reflect-policy.nra#L8)
  Keep only the kind, names, identity and layout; dynamic access traps. Since 0.3.0.
- **ReflectPolicy** · [`export let Layout = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/reflect-policy.nra#L12)
  Keep fields, variants, elements and constructors as well; methods and tags are left out. Since 0.3.0.
- **ReflectPolicy** · [`export let Level = enum (Strip, Layout, Full)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/reflect-policy.nra#L16)
  The retention levels, as descriptors report them. Since 0.3.0.
- **ReflectPolicy** · [`export let of_text = func(text: string) -> Level`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/reflect-policy.nra#L20)
  The level a descriptor reports in its text form (`strip`, `layout` or `full`). Since 0.3.0.
<!-- END SOURCE API -->

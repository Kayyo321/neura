# `Std.Reflect.Type.TypeKind`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Type.TypeKind`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/type/type-kind.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. `typeof(...)` ([Core language](CoreLanguage#typeof)) returns a `Std.Reflect.Core.TypeInfo` whose kind is a `Std.Reflect.Type.TypeKind`; the rest of `Std.Reflect` is a later addition.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TypeKind** · [`export let TypeKind = enum ( Nil, Bool, Integer, Float, String, Class, Interface, Record, Enum, Tuple, Array, List, Function, Any, TypeParameter, Other )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/type/type-kind.nra#L8)
  One kind of type. The order is part of the contract: `typeof` passes a kind as its ordinal. Since 0.3.0.
- **TypeKind** · [`export let of_ordinal = func(ordinal: int) -> TypeKind`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/type/type-kind.nra#L16)
  The kind whose ordinal is `ordinal`, or Other for an ordinal outside the enum. Tagged `#Pure`, `#ThreadSafe`. Since 0.3.0.
<!-- END SOURCE API -->

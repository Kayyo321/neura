# `Std.Reflect.Core.TypeInfo`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Core.TypeInfo`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/type-info.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. `typeof(...)` ([Core language](CoreLanguage#typeof)) returns a `Std.Reflect.Core.TypeInfo` whose kind is a `Std.Reflect.Type.TypeKind`; the rest of `Std.Reflect` is a later addition.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TypeInfo** · [`export let TypeInfo = record (kind: kinds.TypeKind, name: string, qualified_name: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/type-info.nra#L12)
  A described type. As a record it compares, hashes and prints by value: `typeof(a) === typeof(b)` compares types, `==` compares identity. Since 0.3.0.
- **TypeInfo** · [`export let from_parts = func(parts: [int, string, string]) -> TypeInfo`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/type-info.nra#L18)
  The record for the parts the compiler passes for one `typeof`: the kind's ordinal, the name and the qualified name. Programs call `typeof` instead. Tagged `#Pure`, `#ThreadSafe`. Since 0.3.0.
<!-- END SOURCE API -->

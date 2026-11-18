# `Std.Reflect.Core.TypeId`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Core.TypeId`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/type-id.nra) defines the shipped declarations: a deterministic 128-bit type fingerprint. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. [Section 20 of the standard library specification](Stdlib-Design#22-stdreflect) describes `Std.Reflect` as a whole.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TypeId** · [`export let TypeId = record (high: uint64, low: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/type-id.nra#L13)
  The fingerprint as two 64-bit halves, most significant first. As a record it compares, hashes and prints by value. Since 0.3.0.
- **TypeId** · [`export let of_identity = func(identity: string) -> TypeId`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/type-id.nra#L17)
  The fingerprint of an identity string. Since 0.3.0.
- **TypeId** · [`export let to_hex = func(id: TypeId) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/type-id.nra#L30)
  The fingerprint as 32 lowercase hexadecimal digits. Since 0.3.0.
<!-- END SOURCE API -->

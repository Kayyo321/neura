# `Std.Reflect.Member.Members`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Member.Members`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/members.nra) defines the shipped declarations: field, variant, method, parameter and constructor descriptors. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. [Section 20 of the standard library specification](Stdlib-Design#20-stdreflect) describes `Std.Reflect` as a whole.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Members** · [`export let FieldInfo = model.FieldInfo`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/members.nra#L11)
  A field of a class or record. Since 0.3.0.
- **Members** · [`export let VariantInfo = model.VariantInfo`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/members.nra#L15)
  A variant of an enum and its payloads. Since 0.3.0.
- **Members** · [`export let MethodInfo = model.MethodInfo`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/members.nra#L19)
  An instance method. Since 0.3.0.
- **Members** · [`export let ParamInfo = model.ParamInfo`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/members.nra#L23)
  A parameter, or a payload of a variant. Since 0.3.0.
- **Members** · [`export let ConstructorInfo = model.ConstructorInfo`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/members.nra#L27)
  A constructor of a class or record. Since 0.3.0.
- **Members** · [`export let Visibility = enum (Exported, Private)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/members.nra#L31)
  Whether a member is visible outside its declaration's module. Since 0.3.0.
- **Members** · [`export let FieldFlags = record (constant: bool, last: bool, property: bool, secret: bool, skipped: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/members.nra#L35)
  The flags of a field. `Secret` is the `#Sensitive` tag; Neura has no volatile fields. Since 0.3.0.
- **Members** · [`export let visibility = func(field: FieldInfo) -> Visibility`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/members.nra#L39)
  The visibility of a field. Since 0.3.0.
- **Members** · [`export let flags = func(field: FieldInfo) -> FieldFlags`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/members.nra#L43)
  The flags of a field. Since 0.3.0.
- **Members** · [`export let properties = func(type: model.TypeDescriptor) -> FieldInfo[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/members.nra#L50)
  The fields that are properties: read and written through accessor clauses rather than storage. Since 0.3.0.
<!-- END SOURCE API -->

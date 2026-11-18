# `Std.Reflect.Codec.Walk`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Codec.Walk`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/walk.nra) defines the shipped declarations: structural walks with visitors. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. [Section 20 of the standard library specification](Stdlib-Design#20-stdreflect) describes `Std.Reflect` as a whole.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Walk** · [`export let WalkControl = enum (Continue, SkipChildren, Stop, Cycle)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/walk.nra#L13)
  What a visitor asks for after seeing a value, and how a walk ended. Since 0.3.0.
- **Walk** · [`export let ReflectVisitor = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/walk.nra#L17)
  A read-only visitor. `path` is the JSON-like path of the value (`$`, `$.name`, `$[2]`). Since 0.3.0.
- **Walk.ReflectVisitor** · [`export let enter = func(path: string, value: model.Variant, control: WalkControl) -> WalkControl`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/walk.nra#L19)
  Called before the value's children. `Cycle` is passed for an object already open on the path.
- **Walk.ReflectVisitor** · [`export let leave = func(path: string, value: model.Variant)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/walk.nra#L21)
  Called after the value's children, unless the walk stopped.
- **Walk** · [`export let ReflectMutVisitor = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/walk.nra#L27)
  A visitor that may replace field values: `visit_field` returns the value to store, which is written through `Variant.set_field` when it differs from the current one. Since 0.3.0.
- **Walk.ReflectMutVisitor** · [`export let visit_field = func(owner: model.Variant, field: model.FieldInfo, value: model.Variant) -> model.Variant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/walk.nra#L28)
- **Walk** · [`export let WalkEngine = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/walk.nra#L33)
  The walker. It allocates only its path stack. Since 0.3.0.
- **Walk.WalkEngine** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/walk.nra#L34)
- **Walk.WalkEngine** · [`export let walk = func(root: model.Variant, visitor: ReflectVisitor) -> WalkControl`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/walk.nra#L37)
  Walks `root` with `visitor`; the result is `Stop` when the visitor stopped the walk.
- **Walk.WalkEngine** · [`export let walk_mut = func(root: model.Variant, visitor: ReflectMutVisitor)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/walk.nra#L43)
  Walks the fields of every object under `root`, letting `visitor` replace each field's value.
- **Walk.WalkEngine** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/codec/walk.nra#L115)
<!-- END SOURCE API -->

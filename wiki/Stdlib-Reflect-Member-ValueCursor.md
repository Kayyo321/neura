# `Std.Reflect.Member.ValueCursor`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Member.ValueCursor`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra) defines the shipped declarations: cursors over a value's structure. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. [Section 20 of the standard library specification](Stdlib-Design#22-stdreflect) describes `Std.Reflect` as a whole.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ValueCursor** · [`export let ValueCursor = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L11)
  A read-only position in a value. Since 0.3.0.
- **ValueCursor.ValueCursor** · [`export last root: model.Variant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L12)
- **ValueCursor.ValueCursor** · [`export last path: int[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L13)
- **ValueCursor.ValueCursor** · [`export let init = func(value: model.Variant, steps: int[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L15)
- **ValueCursor.ValueCursor** · [`export let value = func -> model.Variant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L21)
  The value at the cursor.
- **ValueCursor.ValueCursor** · [`export let descriptor = func -> model.TypeDescriptor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L30)
  The type of the value at the cursor.
- **ValueCursor.ValueCursor** · [`export let child = func(index: int) -> ValueCursor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L33)
  A cursor one step deeper.
- **ValueCursor.ValueCursor** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L35)
- **ValueCursor.ValueCursor** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L36)
- **ValueCursor.ValueCursor** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L43)
- **ValueCursor** · [`export let ValueCursorMut = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L49)
  A position whose field can be written: the last step names a field of the object the rest of the path reaches. Since 0.3.0.
- **ValueCursor.ValueCursorMut** · [`export last root: model.Variant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L50)
- **ValueCursor.ValueCursorMut** · [`export last path: int[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L51)
- **ValueCursor.ValueCursorMut** · [`export let init = func(value: model.Variant, steps: int[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L53)
- **ValueCursor.ValueCursorMut** · [`export let value = func -> model.Variant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L59)
  The value at the cursor.
- **ValueCursor.ValueCursorMut** · [`export let write = func(replacement: model.Variant)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L62)
  Writes the field at the cursor; an empty path or a value of another type traps.
- **ValueCursor.ValueCursorMut** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L73)
- **ValueCursor.ValueCursorMut** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L74)
- **ValueCursor.ValueCursorMut** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L81)
- **ValueCursor** · [`export let at = func(value: model.Variant) -> ValueCursor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/member/value-cursor.nra#L86)
  A cursor at the root of `value`. Since 0.3.0.
<!-- END SOURCE API -->

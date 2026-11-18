# `Std.Reflect.Core.MemoryLayout`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Core.MemoryLayout`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/memory-layout.nra) defines the shipped declarations: the NVR representation of a type. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. [Section 20 of the standard library specification](Stdlib-Design#20-stdreflect) describes `Std.Reflect` as a whole.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **MemoryLayout** · [`export let Form = enum (Word, Tuple, Enum, Reference, Any)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/memory-layout.nra#L10)
  The shape of a value: one word, an inline tuple, an inline payload enum, a class reference, or an `any` (tag, scalar, text and reference words). Since 0.3.0.
- **MemoryLayout** · [`export let MemoryLayout = record (words: int, form: Form, plain: bool, bits: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/memory-layout.nra#L15)
  The value words a value occupies, its form, whether it is plain data (no reference, text or `any` word), and the bit width of a scalar (0 for other types). Since 0.3.0.
- **MemoryLayout** · [`export let form_of_text = func(text: string) -> Form`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/core/memory-layout.nra#L19)
  The form a descriptor reports in its text form. Since 0.3.0.
<!-- END SOURCE API -->

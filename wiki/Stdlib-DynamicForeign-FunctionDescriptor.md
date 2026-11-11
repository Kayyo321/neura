# `Std.DynamicForeign.FunctionDescriptor`

[Standard library index](Stdlib) · [Foreign interop](ForeignInterop) · [Language status](Status)

Import this module as `Std.DynamicForeign.FunctionDescriptor`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/function-descriptor.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## Detailed contracts

[Foreign interop](ForeignInterop) defines the safety, determinism, and capability rules shared by every `Std.DynamicForeign` module. Native operations are granted by default and denied with `--deny-native`; when denied, the first one traps with `invalid state: native access denied: the native capability is not granted`.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **FunctionDescriptor** · [`export let FunctionDescriptor = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/function-descriptor.nra#L9)
  A result layout, or none for void, and the argument layouts in order.
- **FunctionDescriptor.FunctionDescriptor** · [`export let init = func(result_layout: Optional<MemoryLayout>, argument_layouts: MemoryLayout[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/function-descriptor.nra#L16)
  Use of or of_void.
- **FunctionDescriptor.FunctionDescriptor** · [`export let return_layout = func -> Optional<MemoryLayout>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/function-descriptor.nra#L25)
  The result layout, or None for void.
- **FunctionDescriptor.FunctionDescriptor** · [`export let argument_layouts = func -> MemoryLayout[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/function-descriptor.nra#L28)
  A copy of the argument layouts.
- **FunctionDescriptor.FunctionDescriptor** · [`export let append_argument_layouts = func(more: ...MemoryLayout) -> FunctionDescriptor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/function-descriptor.nra#L37)
  A descriptor with `more` appended to the arguments.
- **FunctionDescriptor.FunctionDescriptor** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/function-descriptor.nra#L46)
  Descriptors with the same layouts are equal.
- **FunctionDescriptor.FunctionDescriptor** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/function-descriptor.nra#L54)
  Hashes the text form.
- **FunctionDescriptor.FunctionDescriptor** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/function-descriptor.nra#L57)
  A form such as `(i32i32)i32` or `(a)v`.
- **FunctionDescriptor** · [`export let of = func(result: MemoryLayout, arguments: ...MemoryLayout) -> FunctionDescriptor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/function-descriptor.nra#L77)
  A function returning `result` and taking `arguments` (Java's FunctionDescriptor.of).
- **FunctionDescriptor** · [`export let of_void = func(arguments: ...MemoryLayout) -> FunctionDescriptor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/function-descriptor.nra#L83)
  A void function taking `arguments` (Java's FunctionDescriptor.ofVoid).
<!-- END SOURCE API -->

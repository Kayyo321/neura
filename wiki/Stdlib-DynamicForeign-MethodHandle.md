# `Std.DynamicForeign.MethodHandle`

[Standard library index](Stdlib) · [Foreign interop](ForeignInterop) · [Language status](Status)

Import this module as `Std.DynamicForeign.MethodHandle`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## Detailed contracts

[Foreign interop](ForeignInterop) defines the safety, determinism, and capability rules shared by every `Std.DynamicForeign` module. Native operations need `--allow-native`; without it the first one traps with `invalid state: native access denied: run with --allow-native`.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **MethodHandle** · [`export let MethodHandle = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L21)
  A native function address bound to a descriptor.
- **MethodHandle.MethodHandle** · [`export let init = func(symbol: MemorySegment, descriptor: FunctionDescriptor, variadic_index: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L32)
  Use Linker.downcall_handle.
- **MethodHandle.MethodHandle** · [`export let descriptor = func -> FunctionDescriptor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L43)
  The C signature.
- **MethodHandle.MethodHandle** · [`export let address = func -> MemorySegment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L46)
  The function's address.
- **MethodHandle.MethodHandle** · [`export let invoke = func(args: ...) -> ForeignValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L51)
  Calls the function. Integers, floats, and booleans convert to the declared layouts when they fit; a string passes a NUL-terminated copy that lives for the call; a MemorySegment passes its address, or its bytes for a struct; nil passes a null pointer.
- **MethodHandle.MethodHandle** · [`export let invoke_with = func(allocator: Arena, args: ...) -> ForeignValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L54)
  invoke, copying a struct result into `allocator`.
- **MethodHandle.MethodHandle** · [`export let invoke_values = func(values: ForeignValue[list]) -> ForeignValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L89)
  Calls the function with values already tagged by carrier; each must match its layout exactly.
- **MethodHandle.MethodHandle** · [`export let invoke_values_with = func(allocator: Arena, values: ForeignValue[list]) -> ForeignValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L93)
  invoke_values, copying a struct result into `allocator`.
<!-- END SOURCE API -->

# `Std.DynamicForeign.MethodHandle`

[Standard library index](Stdlib) · [Foreign interop](ForeignInterop) · [Language status](Status)

Import this module as `Std.DynamicForeign.MethodHandle`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## Detailed contracts

[Foreign interop](ForeignInterop) defines the safety, determinism, and capability rules shared by every `Std.DynamicForeign` module. Native operations are granted by default and denied with `--deny-native`; when denied, the first one traps with `invalid state: native access denied: the native capability is not granted`.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **MethodHandle** · [`export let MethodHandle = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L24)
  A native function address bound to a descriptor. Tagged `#Final`, `#NotThreadSafe`.
- **MethodHandle.MethodHandle** · [`export let init = func(symbol: MemorySegment, descriptor: FunctionDescriptor, variadic_index: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L35)
  Use Linker.downcall_handle.
- **MethodHandle.MethodHandle** · [`export let descriptor = func -> FunctionDescriptor`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L47)
  The C signature. Tagged `#Pure`.
- **MethodHandle.MethodHandle** · [`export let address = func -> MemorySegment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L51)
  The function's address. Tagged `#Pure`.
- **MethodHandle.MethodHandle** · [`export let invoke = func(args: ...) -> ForeignValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L56)
  Calls the function. Integers, floats, and booleans convert to the declared layouts when they fit; a string passes a NUL-terminated copy that lives for the call; a MemorySegment passes its address, or its bytes for a struct; nil passes a null pointer.
- **MethodHandle.MethodHandle** · [`export let invoke_with = func(allocator: Arena, args: ...) -> ForeignValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L59)
  invoke, copying a struct result into `allocator`.
- **MethodHandle.MethodHandle** · [`export static let call_variadic = func(symbol: MemorySegment, fixed: FunctionDescriptor, args: Argument[list]) -> ForeignValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L66)
  Calls a variadic C function whose named parameters `fixed` describes with `args`: the named arguments, then the variadic ones. Each variadic argument takes the layout C's default argument promotions give it: an int when an integer fits one and a long long (or unsigned long long) otherwise, a double for a float, an int for a bool, and an address for a string, a MemorySegment, or nil.
- **MethodHandle.MethodHandle** · [`export let invoke_values = func(values: ForeignValue[list]) -> ForeignValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L143)
  Calls the function with values already tagged by carrier; each must match its layout exactly.
- **MethodHandle.MethodHandle** · [`export let invoke_values_with = func(allocator: Arena, values: ForeignValue[list]) -> ForeignValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/method-handle.nra#L147)
  invoke_values, copying a struct result into `allocator`.
<!-- END SOURCE API -->

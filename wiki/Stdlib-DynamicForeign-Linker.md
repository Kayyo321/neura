# `Std.DynamicForeign.Linker`

[Standard library index](Stdlib) · [Foreign interop](ForeignInterop) · [Language status](Status)

Import this module as `Std.DynamicForeign.Linker`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/linker.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## Detailed contracts

[Foreign interop](ForeignInterop) defines the safety, determinism, and capability rules shared by every `Std.DynamicForeign` module. Native operations are granted by default and denied with `--deny-native`; when denied, the first one traps with `invalid state: native access denied: the native capability is not granted`.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Linker** · [`export let Linker = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/linker.nra#L22)
  The platform's C calling convention. Tagged `#Final`, `#NotThreadSafe`.
- **Linker.Linker** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/linker.nra#L24)
  Use native_linker.
- **Linker.Linker** · [`export let downcall_handle = func(symbol: MemorySegment, descriptor: FunctionDescriptor) -> MethodHandle`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/linker.nra#L29)
  A handle that calls the function at `symbol` with the C signature `descriptor` (Java's Linker.downcallHandle). Tagged `#Pure`.
- **Linker.Linker** · [`export let upcall_stub = func(target: func(ForeignValue[list]) -> ForeignValue, descriptor: FunctionDescriptor, arena: Arena) -> MemorySegment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/linker.nra#L47)
  A native function pointer that calls `target` with the C signature `descriptor` (Java's Linker.upcallStub), owned by `arena`. Native code may call it only during a downcall from this program and while the arena is open; a call at any other time ends the process with status 134. `target` receives the arguments as ForeignValues and returns the result, or ForeignValue.Void for a void descriptor; a struct argument is a copy that lives until `target` returns. A trap in `target` ends the run when the downcall returns. A variadic signature cannot be an upcall.
- **Linker.Linker** · [`export let default_lookup = func -> lookups.SymbolLookup`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/linker.nra#L57)
  The libraries already loaded into the process, including the C library (Java's Linker.defaultLookup).
- **Linker.Linker** · [`export let canonical_layout = func(name: string) -> Optional<MemoryLayout>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/linker.nra#L62)
  The layout of a C type on the runner's platform (Java's Linker.canonicalLayouts): "bool", "char", "short", "int", "long", "long long", "float", "double", "void*", "size_t", or "wchar_t". None for any other name.
- **Linker** · [`export let native_linker = func -> Linker`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/linker.nra#L110)
  The linker for the runner's platform C ABI.
<!-- END SOURCE API -->

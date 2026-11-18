# `Std.Reflect.Ffi.ForeignCall`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Ffi.ForeignCall`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra) defines the shipped declarations: native calls with reflected arguments. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. [Section 20 of the standard library specification](Stdlib-Design#20-stdreflect) describes `Std.Reflect` as a whole.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ForeignCall** · [`export let ForeignSymbol = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L23)
  A native function: its name, address, C signature and the target it follows. Since 0.3.0.
- **ForeignCall.ForeignSymbol** · [`export last name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L24)
- **ForeignCall.ForeignSymbol** · [`export last address: segments.MemorySegment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L25)
- **ForeignCall.ForeignSymbol** · [`export last target: abi.Target`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L26)
- **ForeignCall.ForeignSymbol** · [`export last returns_void: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L27)
- **ForeignCall.ForeignSymbol** · [`export last result: abi.CType`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L28)
- **ForeignCall.ForeignSymbol** · [`export last parameters: abi.CType[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L29)
- **ForeignCall.ForeignSymbol** · [`export let init = func(label: string, at: segments.MemorySegment, on: abi.Target, no_result: bool, result_type: abi.CType, parameter_types: abi.CType[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L31)
- **ForeignCall.ForeignSymbol** · [`export let frame = func -> CallFrame`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L42)
  A new frame for one call.
- **ForeignCall.ForeignSymbol** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L44)
- **ForeignCall** · [`export let CallFrame = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L57)
  The arguments of one call. Each argument is checked against its C type when the call is made: an integer type takes a Variant of the Neura integer of the same width and signedness, `Float` a float32, `Double` a float64, `Bool` a bool, and `Pointer` a `Std.DynamicForeign.MemorySegment.MemorySegment`. Since 0.3.0.
- **ForeignCall.CallFrame** · [`export last symbol: ForeignSymbol`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L58)
- **ForeignCall.CallFrame** · [`export last arguments: model.Variant[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L59)
- **ForeignCall.CallFrame** · [`export let init = func(target: ForeignSymbol)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L61)
- **ForeignCall.CallFrame** · [`export let push = func(value: model.Variant) -> CallFrame`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L67)
  Adds the next argument and returns this frame.
- **ForeignCall.CallFrame** · [`export let invoke = func -> model.Variant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L74)
  Makes the call; the result is a Variant of the result's Neura type, nil for `void`. A wrong argument count or type traps before the call.
- **ForeignCall.CallFrame** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L91)
- **ForeignCall** · [`export let lookup = func(name: string, on: abi.Target, returns_void: bool, result: abi.CType, parameters: abi.CType[list]) -> Optional<ForeignSymbol>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L97)
  The native function `name` in the default lookup of the running program (the C library and the program's own native code), or None. Since 0.3.0.
- **ForeignCall** · [`export let lookup_in = func(library: lookups.SymbolLookup, name: string, on: abi.Target, returns_void: bool, result: abi.CType, parameters: abi.CType[list]) -> Optional<ForeignSymbol>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L103)
  The native function `name` in `library`, or None. Since 0.3.0.
- **ForeignCall** · [`export let value_layout = func(type: abi.CType, on: abi.Target) -> memory_layouts.MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/foreign-call.nra#L117)
  The `Std.DynamicForeign` layout of a C scalar on the target. Since 0.3.0.
<!-- END SOURCE API -->

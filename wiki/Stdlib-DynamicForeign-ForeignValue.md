# `Std.DynamicForeign.ForeignValue`

[Standard library index](Stdlib) · [Foreign interop](ForeignInterop) · [Language status](Status)

Import this module as `Std.DynamicForeign.ForeignValue`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/foreign-value.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## Detailed contracts

[Foreign interop](ForeignInterop) defines the safety, determinism, and capability rules shared by every `Std.DynamicForeign` module. Native operations are granted by default and denied with `--deny-native`; when denied, the first one traps with `invalid state: native access denied: run with --allow-native`.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ForeignValue** · [`export let ForeignValue = enum ( Void, Bool(bool), Int8(int8), UInt8(uint8), Int16(int16), UInt16(uint16), Int32(int32), UInt32(uint32), Int64(int64), UInt64(uint64), Float32(float32), Float64(float64), Address(MemorySegment), Struct(MemorySegment) )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/foreign-value.nra#L9)
  A native argument or result, tagged by its carrier.
- **ForeignValue** · [`export let as_bool = func(value: ForeignValue) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/foreign-value.nra#L32)
  The value of a Bool result.
- **ForeignValue** · [`export let as_int32 = func(value: ForeignValue) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/foreign-value.nra#L40)
  The value of any signed or unsigned integer result that fits in an int32.
- **ForeignValue** · [`export let as_int64 = func(value: ForeignValue) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/foreign-value.nra#L43)
  The value of any integer result that fits in an int64, or of a Bool as 0 or 1.
- **ForeignValue** · [`export let as_uint64 = func(value: ForeignValue) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/foreign-value.nra#L59)
  The value of a UInt64 result, or of any non-negative integer result.
- **ForeignValue** · [`export let as_float32 = func(value: ForeignValue) -> float32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/foreign-value.nra#L67)
  The value of a Float32 result.
- **ForeignValue** · [`export let as_float64 = func(value: ForeignValue) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/foreign-value.nra#L75)
  The value of a Float32 or Float64 result as a float64.
- **ForeignValue** · [`export let as_segment = func(value: ForeignValue) -> MemorySegment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/foreign-value.nra#L84)
  The segment of an Address or Struct result.
- **ForeignValue** · [`export let describe = func(value: ForeignValue) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/foreign-value.nra#L93)
  A short description such as `Int32(8)`.
<!-- END SOURCE API -->

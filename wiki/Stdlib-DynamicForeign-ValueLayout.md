# `Std.DynamicForeign.ValueLayout`

[Standard library index](Stdlib) · [Foreign interop](ForeignInterop) · [Language status](Status)

Import this module as `Std.DynamicForeign.ValueLayout`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/value-layout.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## Detailed contracts

[Foreign interop](ForeignInterop) defines the safety, determinism, and capability rules shared by every `Std.DynamicForeign` module. Native operations are granted by default and denied with `--deny-native`; when denied, the first one traps with `invalid state: native access denied: run with --allow-native`.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ValueLayout** · [`export const Bool = value(Carrier.Bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/value-layout.nra#L11)
  A C `_Bool`, one byte that is 0 or 1, carried as bool.
- **ValueLayout** · [`export const Int8 = value(Carrier.Int8)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/value-layout.nra#L13)
  A signed byte, carried as int8.
- **ValueLayout** · [`export const UInt8 = value(Carrier.UInt8)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/value-layout.nra#L15)
  An unsigned byte, carried as uint8.
- **ValueLayout** · [`export const Int16 = value(Carrier.Int16)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/value-layout.nra#L17)
  A 16-bit signed integer, carried as int16.
- **ValueLayout** · [`export const UInt16 = value(Carrier.UInt16)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/value-layout.nra#L19)
  A 16-bit unsigned integer, carried as uint16.
- **ValueLayout** · [`export const Int32 = value(Carrier.Int32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/value-layout.nra#L21)
  A 32-bit signed integer, carried as int32 (Java's JAVA_INT).
- **ValueLayout** · [`export const UInt32 = value(Carrier.UInt32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/value-layout.nra#L23)
  A 32-bit unsigned integer, carried as uint32.
- **ValueLayout** · [`export const Int64 = value(Carrier.Int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/value-layout.nra#L25)
  A 64-bit signed integer, carried as int64 (Java's JAVA_LONG).
- **ValueLayout** · [`export const UInt64 = value(Carrier.UInt64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/value-layout.nra#L27)
  A 64-bit unsigned integer, carried as uint64.
- **ValueLayout** · [`export const Float32 = value(Carrier.Float32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/value-layout.nra#L29)
  An IEEE binary32 float, carried as float32 (Java's JAVA_FLOAT).
- **ValueLayout** · [`export const Float64 = value(Carrier.Float64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/value-layout.nra#L31)
  An IEEE binary64 float, carried as float64 (Java's JAVA_DOUBLE).
- **ValueLayout** · [`export const Address = value(Carrier.Address)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/value-layout.nra#L33)
  A native pointer, carried as a MemorySegment (Java's ADDRESS).
<!-- END SOURCE API -->

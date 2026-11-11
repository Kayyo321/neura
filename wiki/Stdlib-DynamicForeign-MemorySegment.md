# `Std.DynamicForeign.MemorySegment`

[Standard library index](Stdlib) · [Foreign interop](ForeignInterop) · [Language status](Status)

Import this module as `Std.DynamicForeign.MemorySegment`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## Detailed contracts

[Foreign interop](ForeignInterop) defines the safety, determinism, and capability rules shared by every `Std.DynamicForeign` module. Native operations are granted by default and denied with `--deny-native`; when denied, the first one traps with `invalid state: native access denied: the native capability is not granted`.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **MemorySegment** · [`export let MemorySegment = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L15)
  A native address, a size in bytes, and the scope that owns the memory.
- **MemorySegment.MemorySegment** · [`export let init = func(address_value: int64, size: int64, scope: scopes.Scope)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L24)
  Use Arena.allocate or of_address; this constructor trusts its arguments.
- **MemorySegment.MemorySegment** · [`export let address = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L31)
  The native address of the first byte.
- **MemorySegment.MemorySegment** · [`export let byte_size = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L34)
  The number of accessible bytes.
- **MemorySegment.MemorySegment** · [`export let is_null = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L37)
  Whether the address is 0.
- **MemorySegment.MemorySegment** · [`export let is_alive = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L40)
  Whether the owning arena is still open.
- **MemorySegment.MemorySegment** · [`export let scope = func -> scopes.Scope`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L43)
  The scope that owns the memory; library code passes it on to derived segments.
- **MemorySegment.MemorySegment** · [`export let reinterpret = func(size: int64) -> MemorySegment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L47)
  The same address with `size` accessible bytes, for memory whose extent native code defines. Like Java's reinterpret, this is unsafe: the size is not checked against the real allocation.
- **MemorySegment.MemorySegment** · [`export let as_slice = func(offset: int64, size: int64) -> MemorySegment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L55)
  The `size` bytes starting at `offset`, sharing this segment's scope.
- **MemorySegment.MemorySegment** · [`export let as_slice_from = func(offset: int64) -> MemorySegment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L61)
  The bytes from `offset` to the end, sharing this segment's scope.
- **MemorySegment.MemorySegment** · [`export let get_bool = func(offset: int64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L64)
  Reads a C `_Bool`; any nonzero byte is true.
- **MemorySegment.MemorySegment** · [`export let get_int8 = func(offset: int64) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L66)
  Reads a signed byte.
- **MemorySegment.MemorySegment** · [`export let get_uint8 = func(offset: int64) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L68)
  Reads an unsigned byte.
- **MemorySegment.MemorySegment** · [`export let get_int16 = func(offset: int64) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L70)
  Reads a 16-bit signed integer.
- **MemorySegment.MemorySegment** · [`export let get_uint16 = func(offset: int64) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L72)
  Reads a 16-bit unsigned integer.
- **MemorySegment.MemorySegment** · [`export let get_int32 = func(offset: int64) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L74)
  Reads a 32-bit signed integer.
- **MemorySegment.MemorySegment** · [`export let get_uint32 = func(offset: int64) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L76)
  Reads a 32-bit unsigned integer.
- **MemorySegment.MemorySegment** · [`export let get_int64 = func(offset: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L78)
  Reads a 64-bit signed integer.
- **MemorySegment.MemorySegment** · [`export let get_uint64 = func(offset: int64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L80)
  Reads a 64-bit unsigned integer.
- **MemorySegment.MemorySegment** · [`export let get_float32 = func(offset: int64) -> float32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L82)
  Reads an IEEE binary32 float.
- **MemorySegment.MemorySegment** · [`export let get_float64 = func(offset: int64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L84)
  Reads an IEEE binary64 float.
- **MemorySegment.MemorySegment** · [`export let get_address = func(offset: int64) -> MemorySegment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L86)
  Reads a pointer as a segment of size 0 in the global scope.
- **MemorySegment.MemorySegment** · [`export let set_bool = func(offset: int64, value: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L90)
  Writes a C `_Bool` as 0 or 1.
- **MemorySegment.MemorySegment** · [`export let set_int8 = func(offset: int64, value: int8)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L92)
  Writes a signed byte.
- **MemorySegment.MemorySegment** · [`export let set_uint8 = func(offset: int64, value: uint8)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L94)
  Writes an unsigned byte.
- **MemorySegment.MemorySegment** · [`export let set_int16 = func(offset: int64, value: int16)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L96)
  Writes a 16-bit signed integer.
- **MemorySegment.MemorySegment** · [`export let set_uint16 = func(offset: int64, value: uint16)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L99)
  Writes a 16-bit unsigned integer.
- **MemorySegment.MemorySegment** · [`export let set_int32 = func(offset: int64, value: int32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L101)
  Writes a 32-bit signed integer.
- **MemorySegment.MemorySegment** · [`export let set_uint32 = func(offset: int64, value: uint32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L104)
  Writes a 32-bit unsigned integer.
- **MemorySegment.MemorySegment** · [`export let set_int64 = func(offset: int64, value: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L106)
  Writes a 64-bit signed integer.
- **MemorySegment.MemorySegment** · [`export let set_uint64 = func(offset: int64, value: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L108)
  Writes a 64-bit unsigned integer.
- **MemorySegment.MemorySegment** · [`export let set_float32 = func(offset: int64, value: float32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L110)
  Writes an IEEE binary32 float.
- **MemorySegment.MemorySegment** · [`export let set_float64 = func(offset: int64, value: float64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L113)
  Writes an IEEE binary64 float.
- **MemorySegment.MemorySegment** · [`export let set_address = func(offset: int64, value: MemorySegment)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L115)
  Writes the address of `value`.
- **MemorySegment.MemorySegment** · [`export let get_utf8 = func(offset: int64) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L120)
  The UTF-8 text from `offset` up to the first NUL byte, which must lie inside the segment. Traps with "invalid argument: unterminated string" or "invalid argument: text is not valid UTF-8".
- **MemorySegment.MemorySegment** · [`export let set_utf8 = func(offset: int64, text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L135)
  Writes the UTF-8 bytes of `text` and a NUL at `offset`. Traps if `text` contains a NUL.
- **MemorySegment.MemorySegment** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L149)
  A copy of all the bytes.
- **MemorySegment.MemorySegment** · [`export let copy_from_bytes = func(offset: int64, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L155)
  Copies `bytes` into the segment at `offset`.
- **MemorySegment.MemorySegment** · [`export let copy_from = func(source: MemorySegment)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L161)
  Copies all of `source` to the start of this segment.
- **MemorySegment.MemorySegment** · [`export let fill = func(value: uint8)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L166)
  Sets every byte to `value`.
- **MemorySegment.MemorySegment** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L175)
  Segments are equal when their addresses and sizes are.
- **MemorySegment.MemorySegment** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L184)
  Hashes the address and size.
- **MemorySegment.MemorySegment** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L187)
  `MemorySegment(address, size)` with the address in hexadecimal.
- **MemorySegment.MemorySegment** · [`export let check = func(offset: int64, size: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L191)
  Traps unless the scope is open and `size` bytes at `offset` lie inside the segment.
- **MemorySegment** · [`export let null_segment = func -> MemorySegment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L233)
  The null address, with size 0.
- **MemorySegment** · [`export let of_address = func(address: int64) -> MemorySegment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-segment.nra#L237)
  A segment of size 0 at a raw address in the global scope (Java's MemorySegment.ofAddress). Use reinterpret to give it a size.
<!-- END SOURCE API -->

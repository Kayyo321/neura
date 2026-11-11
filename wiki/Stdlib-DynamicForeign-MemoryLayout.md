# `Std.DynamicForeign.MemoryLayout`

[Standard library index](Stdlib) · [Foreign interop](ForeignInterop) · [Language status](Status)

Import this module as `Std.DynamicForeign.MemoryLayout`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## Detailed contracts

[Foreign interop](ForeignInterop) defines the safety, determinism, and capability rules shared by every `Std.DynamicForeign` module. Native operations are granted by default and denied with `--deny-native`; when denied, the first one traps with `invalid state: native access denied: the native capability is not granted`.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **MemoryLayout** · [`export let Carrier = enum ( Bool, Int8, UInt8, Int16, UInt16, Int32, UInt32, Int64, UInt64, Float32, Float64, Address )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L10)
  The Neura type that carries a scalar value. Address carries a MemorySegment.
- **MemoryLayout** · [`export let Shape = enum (Value, Struct, Union, Sequence, Padding)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L26)
  The kind of a layout.
- **MemoryLayout** · [`export let MemoryLayout = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L30)
  An immutable description of a region of native memory. Tagged `#Final`.
- **MemoryLayout.MemoryLayout** · [`export let init = func(shape: Shape, carrier: Carrier, byte_size: int64, byte_alignment: int64, members: MemoryLayout[list], member_offsets: int64[list], elements: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L49)
  Use the module functions; this constructor stores precomputed fields.
- **MemoryLayout.MemoryLayout** · [`export let shape = func -> Shape`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L66)
  The kind of the layout. Tagged `#Pure`.
- **MemoryLayout.MemoryLayout** · [`export let byte_size = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L70)
  The size in bytes. Tagged `#Pure`.
- **MemoryLayout.MemoryLayout** · [`export let byte_alignment = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L74)
  The alignment in bytes. Tagged `#Pure`.
- **MemoryLayout.MemoryLayout** · [`export let name = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L78)
  The name, or the empty string when unnamed. Tagged `#Pure`.
- **MemoryLayout.MemoryLayout** · [`export let carrier = func -> Carrier`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L82)
  The scalar carrier. Traps with "invalid argument: not a value layout" for other shapes. Tagged `#Pure`.
- **MemoryLayout.MemoryLayout** · [`export let with_name = func(text: string) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L90)
  A copy of this layout with the name `text`.
- **MemoryLayout.MemoryLayout** · [`export let with_byte_alignment = func(bytes: int64) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L98)
  A copy of this layout with a larger alignment. Traps unless `bytes` is a power of two no smaller than the size's natural alignment requirement allows.
- **MemoryLayout.MemoryLayout** · [`export let member_layouts = func -> MemoryLayout[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L109)
  The members of a struct or union in order, or the element of a sequence. Tagged `#Pure`.
- **MemoryLayout.MemoryLayout** · [`export let member_offsets = func -> int64[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L119)
  The offsets of the members of a struct or union, in the order of member_layouts. Tagged `#Pure`.
- **MemoryLayout.MemoryLayout** · [`export let element_count = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L129)
  The number of elements of a sequence; 0 for other shapes. Tagged `#Pure`.
- **MemoryLayout.MemoryLayout** · [`export let member = func(member_name: string) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L133)
  The member named `member`. Traps with "invalid argument: no member <name>" when absent. Tagged `#Pure`.
- **MemoryLayout.MemoryLayout** · [`export let byte_offset = func(path: ...string) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L137)
  The offset of the member reached by following `path` through named struct and union members. Tagged `#Pure`.
- **MemoryLayout.MemoryLayout** · [`export let element_offset = func(index: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L154)
  The offset of element `index` of a sequence. Traps outside 0 through element_count() - 1. Tagged `#Pure`.
- **MemoryLayout.MemoryLayout** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L179)
  Layouts compare by shape, carrier, size, alignment, name, and members. Tagged `#Pure`.
- **MemoryLayout.MemoryLayout** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L189)
  Hashes the text form. Tagged `#Pure`.
- **MemoryLayout.MemoryLayout** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L193)
  A compact form such as `i32`, `[i32(x)i32(y)](point)`, or `4:i8`. Tagged `#Pure`.
- **MemoryLayout** · [`export let carrier_name = func(carrier: Carrier) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L219)
  The short name of a carrier. Tagged `#Pure`.
- **MemoryLayout** · [`export let carrier_size = func(carrier: Carrier) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L236)
  The fixed size of a carrier; an address has the runner's pointer size, 8 here. Tagged `#Pure`.
- **MemoryLayout** · [`export let value = func(carrier: Carrier) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L253)
  A scalar layout with its natural size and alignment. Tagged `#Pure`.
- **MemoryLayout** · [`export let value_sized = func(carrier: Carrier, size: int64, alignment: int64) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L260)
  A scalar layout with an explicit size and alignment, for C types whose size the runner reports. Tagged `#Pure`.
- **MemoryLayout** · [`export let padding_layout = func(bytes: int64) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L269)
  `bytes` bytes of padding with alignment 1. Tagged `#Pure`.
- **MemoryLayout** · [`export let struct_layout = func(members: ...MemoryLayout) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L278)
  A struct with C's natural layout: each member at the next offset aligned for it, the size rounded up to the largest member alignment. Traps when two members share a nonempty name.
- **MemoryLayout** · [`export let union_layout = func(members: ...MemoryLayout) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L299)
  A union: every member at offset 0, the size the largest member's rounded up to the largest alignment.
- **MemoryLayout** · [`export let sequence_layout = func(elements: int64, element: MemoryLayout) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L320)
  `elements` consecutive copies of `element`, a C array.
- **MemoryLayout** · [`export let align_up = func(offset: int64, alignment: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L333)
  `offset` rounded up to a multiple of `alignment`. Tagged `#Pure`.
<!-- END SOURCE API -->

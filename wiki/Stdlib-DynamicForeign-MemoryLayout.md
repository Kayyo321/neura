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
- **MemoryLayout.MemoryLayout** · [`export let shape = func -> Shape`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L65)
  The kind of the layout.
- **MemoryLayout.MemoryLayout** · [`export let byte_size = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L68)
  The size in bytes.
- **MemoryLayout.MemoryLayout** · [`export let byte_alignment = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L71)
  The alignment in bytes.
- **MemoryLayout.MemoryLayout** · [`export let name = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L74)
  The name, or the empty string when unnamed.
- **MemoryLayout.MemoryLayout** · [`export let carrier = func -> Carrier`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L77)
  The scalar carrier. Traps with "invalid argument: not a value layout" for other shapes.
- **MemoryLayout.MemoryLayout** · [`export let with_name = func(text: string) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L85)
  A copy of this layout with the name `text`.
- **MemoryLayout.MemoryLayout** · [`export let with_byte_alignment = func(bytes: int64) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L93)
  A copy of this layout with a larger alignment. Traps unless `bytes` is a power of two no smaller than the size's natural alignment requirement allows.
- **MemoryLayout.MemoryLayout** · [`export let member_layouts = func -> MemoryLayout[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L103)
  The members of a struct or union in order, or the element of a sequence.
- **MemoryLayout.MemoryLayout** · [`export let member_offsets = func -> int64[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L112)
  The offsets of the members of a struct or union, in the order of member_layouts.
- **MemoryLayout.MemoryLayout** · [`export let element_count = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L121)
  The number of elements of a sequence; 0 for other shapes.
- **MemoryLayout.MemoryLayout** · [`export let member = func(member_name: string) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L124)
  The member named `member`. Traps with "invalid argument: no member <name>" when absent.
- **MemoryLayout.MemoryLayout** · [`export let byte_offset = func(path: ...string) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L127)
  The offset of the member reached by following `path` through named struct and union members.
- **MemoryLayout.MemoryLayout** · [`export let element_offset = func(index: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L143)
  The offset of element `index` of a sequence. Traps outside 0 through element_count() - 1.
- **MemoryLayout.MemoryLayout** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L167)
  Layouts compare by shape, carrier, size, alignment, name, and members.
- **MemoryLayout.MemoryLayout** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L176)
  Hashes the text form.
- **MemoryLayout.MemoryLayout** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L179)
  A compact form such as `i32`, `[i32(x)i32(y)](point)`, or `4:i8`.
- **MemoryLayout** · [`export let carrier_name = func(carrier: Carrier) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L204)
  The short name of a carrier.
- **MemoryLayout** · [`export let carrier_size = func(carrier: Carrier) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L220)
  The fixed size of a carrier; an address has the runner's pointer size, 8 here.
- **MemoryLayout** · [`export let value = func(carrier: Carrier) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L236)
  A scalar layout with its natural size and alignment.
- **MemoryLayout** · [`export let value_sized = func(carrier: Carrier, size: int64, alignment: int64) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L242)
  A scalar layout with an explicit size and alignment, for C types whose size the runner reports.
- **MemoryLayout** · [`export let padding_layout = func(bytes: int64) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L250)
  `bytes` bytes of padding with alignment 1.
- **MemoryLayout** · [`export let struct_layout = func(members: ...MemoryLayout) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L259)
  A struct with C's natural layout: each member at the next offset aligned for it, the size rounded up to the largest member alignment. Traps when two members share a nonempty name.
- **MemoryLayout** · [`export let union_layout = func(members: ...MemoryLayout) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L280)
  A union: every member at offset 0, the size the largest member's rounded up to the largest alignment.
- **MemoryLayout** · [`export let sequence_layout = func(elements: int64, element: MemoryLayout) -> MemoryLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L301)
  `elements` consecutive copies of `element`, a C array.
- **MemoryLayout** · [`export let align_up = func(offset: int64, alignment: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/memory-layout.nra#L313)
  `offset` rounded up to a multiple of `alignment`.
<!-- END SOURCE API -->

# `Std.Reflect.Ffi.Abi`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Ffi.Abi`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra) defines the shipped declarations: C layouts and argument classes for a declared target. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. [Section 20 of the standard library specification](Stdlib-Design#20-stdreflect) describes `Std.Reflect` as a whole.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Abi** · [`export let CallingConvention = enum (SystemV64, Aapcs64, Win64, Wasm32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L13)
  The calling-convention family of a target. Since 0.3.0.
- **Abi** · [`export let Target = record (triple: string, convention: CallingConvention, pointer_bytes: int, long_bytes: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L18)
  A target: its triple, family, and the sizes that differ between C data models (LP64, LLP64, ILP32). Since 0.3.0.
- **Abi** · [`export let CType = enum ( Bool, Char, SChar, UChar, Short, UShort, Int, UInt, Long, ULong, LongLong, ULongLong, Float, Double, Pointer, SizeT, Int8, UInt8, Int16, UInt16, Int32, UInt32, Int64, UInt64 )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L22)
  The C scalar types. Since 0.3.0.
- **Abi** · [`export let CMember = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L29)
  One member of a C struct: a scalar or a nested struct, repeated `count` times (a C array). Since 0.3.0.
- **Abi.CMember** · [`export last name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L30)
- **Abi.CMember** · [`export last scalar: CType`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L31)
- **Abi.CMember** · [`export last nested: CStruct`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L33)
  The nested struct, or nil for a scalar member.
- **Abi.CMember** · [`export last count: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L34)
- **Abi.CMember** · [`export let init = func(label: string, type: CType, inner: CStruct, repeat: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L36)
- **Abi.CMember** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L43)
- **Abi.CMember** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L49)
  The same name, scalar or nested layout text, and count.
- **Abi.CMember** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L56)
- **Abi** · [`export let CStruct = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L61)
  A C struct: its members in declaration order. Since 0.3.0.
- **Abi.CStruct** · [`export last name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L62)
- **Abi.CStruct** · [`export last members: CMember[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L63)
- **Abi.CStruct** · [`export let init = func(label: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L65)
- **Abi.CStruct** · [`export let scalar = func(label: string, type: CType) -> CStruct`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L71)
  Adds a scalar member and returns this struct.
- **Abi.CStruct** · [`export let array = func(label: string, type: CType, count: int) -> CStruct`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L77)
  Adds a C array of `count` scalars and returns this struct.
- **Abi.CStruct** · [`export let nested = func(label: string, inner: CStruct) -> CStruct`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L86)
  Adds a nested struct member and returns this struct.
- **Abi.CStruct** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L91)
- **Abi** · [`export let AbiLayout = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L102)
  The layout of a struct on a target: size, alignment and each member's byte offset. Since 0.3.0.
- **Abi.AbiLayout** · [`export last target: Target`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L103)
- **Abi.AbiLayout** · [`export last size: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L104)
- **Abi.AbiLayout** · [`export last alignment: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L105)
- **Abi.AbiLayout** · [`export last offsets: [string, int][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L106)
- **Abi.AbiLayout** · [`export let init = func(on: Target, bytes: int, align: int, at: [string, int][list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L108)
- **Abi.AbiLayout** · [`export let offset = func(name: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L116)
  The byte offset of the member named `name`; an unknown name traps.
- **Abi.AbiLayout** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L126)
- **Abi.AbiLayout** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L133)
- **Abi.AbiLayout** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L139)
- **Abi** · [`export let ArgumentClass = enum (Integer, Sse, FloatRegisters, GeneralRegisters, Memory, Indirect)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L146)
  How an aggregate argument is passed: per eightbyte in integer or SSE registers (System V), in floating-point registers (an AAPCS64 homogeneous floating-point aggregate), in general registers, or in memory (on the stack, or by a hidden reference on AAPCS64 and Windows x64). Since 0.3.0.
- **Abi** · [`export let target = func(triple: string) -> Optional<Target>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L151)
  The target a triple names: `x86_64-...-linux-gnu`, `x86_64-apple-darwin`, `aarch64-...`, `arm64-apple-darwin`, `x86_64-pc-windows-msvc` or `-gnu`, `wasm32-...`. Another triple returns None. Since 0.3.0.
- **Abi** · [`export let size_of = func(type: CType, on: Target) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L171)
  The size of a scalar in bytes on the target. Since 0.3.0.
- **Abi** · [`export let align_of = func(type: CType, on: Target) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L200)
  The alignment of a scalar in bytes on the target (its size on every supported target). Since 0.3.0.
- **Abi** · [`export let is_float = func(type: CType) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L204)
  Whether a scalar is a floating-point type. Since 0.3.0.
- **Abi** · [`export let layout = func(shape: CStruct, on: Target) -> AbiLayout`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L210)
  The layout of `shape` on `on`, with C's rules: each member at the next multiple of its alignment, the struct aligned to its strictest member and padded to a multiple of that alignment. An empty struct traps (C has none). Since 0.3.0.
- **Abi** · [`export let classify = func(shape: CStruct, on: Target) -> ArgumentClass[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/ffi/abi.nra#L231)
  How `shape` is passed as an argument on `on`: one class per eightbyte on System V x86-64, and one class for the whole value on the other targets. Since 0.3.0.
<!-- END SOURCE API -->

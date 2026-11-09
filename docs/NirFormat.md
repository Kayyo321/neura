# NeuraIR file format

A `.nir` file holds a NeuraIR program as lowered, before optimization. `neura emit-ir <file.nra>... -o <file.nir>` writes one, and `neura build <file.nir> -o <file.nvr>` builds from it with the same verification, optimization, and conversion as a build from source. The [NVM toolchain](runtime/NVM.md#toolchain-and-distribution) defines the file's role. The reader and writer are in [`src/neura-ir/nir_format.cpp`](../src/neura-ir/nir_format.cpp).

The file records every field of the in-memory program: symbols with their kinds, names, owners, inheritance and override links, types, parameters, generics, bounds, and flags, plus every body with its regions, operands, symbol references, literal text, type arguments, and source spans. A file that is written and then read yields a program equal to the original in every field. As a result, a build from `.nir` produces the same image bytes as a build from source. The human-readable listing printed by `neura emit-ir` without `-o` is a separate interface.

## Encoding

Versions 1.0 and 1.1 use the conventions of [NVR](runtime/NVR.md#encoding-and-compatibility): fixed-width little-endian integers, no padding, and a `vector<T>` written as a `u32` count followed by its elements. `text` is a byte vector. A symbol reference is a `u32` index into the symbol table, and `0xffffffff` marks an absent reference where the field is optional.

| Field | Type | Contents |
| --- | --- | --- |
| Magic | 4 bytes | ASCII `NEIR` |
| Major | u16 | 1 |
| Minor | u16 | 0 or 1 |
| Files | vector<text> | Source paths used by spans, in order of first use: symbols first, then bodies depth first |
| Symbols | vector<Symbol> | The symbol table in symbol-index order |
| Bodies | vector<Body> | Bodies in program order |

A file starts with these fields and ends after the last body. A reader accepts major version 1 with minor version 0 or 1. The writer emits 1.1 only when the program uses a 1.1 feature: an operation with a tag above 30, or a loop with a latch. Otherwise it emits 1.0, so files of programs that use none of them are unchanged. A 1.0 file is read with the 1.0 tag table: a tag above 30 or a loop latch in it is an error.

### Records

| Record | Fields in order |
| --- | --- |
| Span | `file:u32` (index into Files, or `0xffffffff` for no file), `line:u64`, `column:u64`, `offset:u64`, `length:u64` |
| Type | `kind:u8`, `name:text`, `symbol:u32` optional, `arguments:vector<Type>` |
| Symbol | `kind:u8`, `name:text`, `span:Span`, `owner:u32` optional, `base:u32` optional, `overrides:u32` optional, `type:Type`, `parameters:vector<u32>`, `generics:vector<u32>`, `has_bound:u8`, `bound:Type` (present only when `has_bound` is 1), `flags:u8` |
| Node | `op:u8`, `type:Type`, `span:Span`, `symbol:u32` optional, `text:text`, `operands:vector<Node>`, `body:vector<Node>`, `alternative:vector<Node>`, `type_arguments:vector<Type>` |
| Body | `owner:u32`, `instructions:vector<Node>` |

Symbol flags are bit 0 exported, bit 1 constant, bit 2 external, and bit 3 has a body. The other bits are zero.

The tags follow the declaration order of the enumerations in [`src/neura-ir/ir.hpp`](../src/neura-ir/ir.hpp):

| Enumeration | Tags |
| --- | --- |
| Type kind | 0 void, 1 bool, 2 string, 3 integer, 4 float, 5 array, 6 list, 7 object, 8 type parameter, 9 module, 10 enum, 11 callable |
| Symbol kind | 0 file, 1 module, 2 class, 3 alias, 4 import, 5 field, 6 function, 7 parameter, 8 local, 9 global, 10 type parameter, 11 enum, 12 variant |
| Operation | 0 initialize, 1 store, 2 load, 3 reference, 4 receiver, 5 field, 6 index, 7 collection, 8 integer, 9 float, 10 string, 11 boolean, 12 unary, 13 binary, 14 short circuit, 15 call, 16 virtual call, 17 external call, 18 base initialize, 19 allocate, 20 length, 21 append, 22 remove at, 23 return, 24 block, 25 branch, 26 loop, 27 break, 28 continue, 29 destroy, 30 discard; in 1.1, 31 convert, 32 collection version, 33 fail |

Adding an operation, symbol kind, or type kind changes these tables and requires a new format version. Region order is evaluation order. A short-circuit node keeps its right operand in `body`. A branch keeps its arms in `body` and `alternative`. A loop keeps its condition as its operand, its statements in `body`, and, in 1.1, its latch in `alternative`: statements that run after the body and on `continue`, before the condition. A convert node changes its operand to the node's type. A collection-version node reads a list's structural version as `uint64`. A fail statement ends the run with the failure its text names; the only one is `iteration`, a list modified during for-each.

Type names in `.nir` files are those lowering writes. Integer types are spelled by their sized names; files written before `int` and `byte` became aliases may still spell them `int` and `byte`, and the backend reads those as `int32` and `uint8`.

## Reading

The reader checks structure and leaves semantics to the [NeuraIR verifier](IrOptimization.md#neurair-verifier), which every build runs on the result. It rejects:

- a missing magic, an unsupported version, or bytes after the last body
- a truncated record, or a count larger than the remaining bytes could hold
- an unknown type kind, symbol kind, or operation tag, a bound flag other than 0 or 1, or reserved flag bits
- a symbol or file reference out of range, or an absent reference where one is required
- type or node nesting deeper than 1,024 levels, which bounds the recursion of the reader and of the passes that walk the program afterward

Paths in spans are recorded as the compiler saw them. They identify source locations for diagnostics and source maps; the build never reads those files. A `.nir` file written on one machine therefore contains that machine's source paths.

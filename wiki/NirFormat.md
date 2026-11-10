# NeuraIR file format

A `.nir` file holds a NeuraIR program as lowered, before optimization. `neura emit-ir <file.nra>... -o <file.nir>` writes one, and `neura build <file.nir> -o <file.nvr>` builds from it with the same verification, optimization, and conversion as a build from source. Source can also [import modules from it](Modules#prebuilt-neurair-modules). The reader and writer are in [`src/neura-ir/nir_format.cpp`](https://github.com/Kayyo321/neura/blob/main/src/neura-ir/nir_format.cpp).

The file records every field of the in-memory program: symbols with their kinds, names, owners, inheritance and override links, types, parameters, generics, bounds, and flags, plus every body with its regions, operands, symbol references, literal text, type arguments, and source spans. A file that is written and then read yields a program equal to the original in every field. As a result, a build from `.nir` produces the same image bytes as a build from source. The human-readable listing printed by `neura emit-ir` without `-o` is a separate interface.

## Encoding

All minor versions use the conventions of [NVR](NVR#encoding-and-compatibility). Integers are fixed-width and little-endian. Records have no padding, and a `vector<T>` is a `u32` count followed by its elements. `text` is a byte vector. A symbol reference is a `u32` index into the symbol table, and `0xffffffff` marks an absent reference where the field is optional.

| Field | Type | Contents |
| --- | --- | --- |
| Magic | 4 bytes | ASCII `NEIR` |
| Major | u16 | 1 |
| Minor | u16 | 0 through 7 |
| Files | vector<text> | Source paths used by spans, in order of first use: symbols first, then bodies depth first |
| Symbols | vector<Symbol> | The symbol table in symbol-index order |
| Bodies | vector<Body> | Bodies in program order |
| Static constants | vector<StaticConstant> | Present starting with 1.6 |

The reader accepts versions 1.0 through 1.7. The writer emits the earliest version that can encode the program. Version 1.1 adds loop latches and operation tags 31–33. Version 1.2 adds tuple types, enum payloads, aggregate and selection operations, comparison chains, and bitwise spellings on existing unary and binary nodes. Version 1.3 adds structural function types, generic enum applications, interfaces and intersection bounds, library and host bindings, nil references and class operations, universal value members, and text, float bit access, and message-trap operations. Version 1.4 adds repeated collection allocation, class static scopes and qualified static field access, and statements before a scoped value's result. Version 1.5 adds guarded static initialization using the existing operations. Version 1.6 preserves source linkage signatures and prepared static constants. Version 1.7 adds primitive reference layouts and operation tag 60. The reader rejects features newer than the file's declared version.

### Records

| Record | Fields in order |
| --- | --- |
| Span | `file:u32` (index into Files, or `0xffffffff` for no file), `line:u64`, `column:u64`, `offset:u64`, `length:u64` |
| Type | `kind:u8`, `name:text`, `symbol:u32` optional, `arguments:vector<Type>` |
| Symbol | `kind:u8`, `name:text`, `span:Span`, `owner:u32` optional, `base:u32` optional, `overrides:u32` optional, `type:Type`, `parameters:vector<u32>`, `generics:vector<u32>`, `has_bound:u8`, `bound:Type` (present only when `has_bound` is 1), `flags:u8`, `payload_types:vector<Type>` (present when flag bit 4 is set) |
| Node | `op:u8`, `type:Type`, `span:Span`, `symbol:u32` optional, `text:text`, `operands:vector<Node>`, `body:vector<Node>`, `alternative:vector<Node>`, `type_arguments:vector<Type>` |
| Body | `owner:u32`, `instructions:vector<Node>` |
| StaticConstant | `field:u32`, `value:Node` |

Symbol flags are bit 0 exported, bit 1 constant, bit 2 external, and bit 3 has a body. Starting with 1.2, bit 4 indicates a nonempty vector of payload types in declaration order. Only variant symbols may declare payload types. Version 1.3 adds bit 5 for a host binding, bit 6 for a library file identity, and bit 7 for interface links or additional bounds. When set, these bits add fields after the optional payload vector, in flag order:

- Host binding: service name as `text`, then version as `u32`.
- Library file identity: nonempty library-relative path as `text`.
- Interfaces and additional bounds: two `vector<Type>` fields, in that order. The first holds implemented or extended interface applications. The second holds the bounds after the first `bound` field. At least one vector is nonempty.

In 1.6, each symbol ends with a `u8` linkage flag after the other optional fields. Bits 0–1 encode fixed arity (0), typed variadic (1), or untyped variadic (2). Bit 2 marks a user-defined object cast. Bit 3 marks an overload branch and adds a required symbol reference to the first branch. Bit 4 marks a singleton class and adds a required reference to its value. References follow in bit order. A prepared static constant records the typed value available at compile time without running its class's initializer.

Earlier artifacts still build directly. When imported into new source, a variadic function from an older project artifact exposes its packed array signature. Rebuild the library to restore variadic calls or expose a static constant whose value was erased during older lowering.

The synthetic `$static` module is a class or interface's static scope.
It owns shared global fields, receiver-free functions, and nested type
declarations independently of the enclosing class's generic parameters.
A static field load or store may carry an object qualifier as an operand
of its `load` node. The qualifier evaluates before the field access and
the value is discarded.

In 1.5, a private, receiver-free `$static_init` function in the static
scope sets a per-type flag before calling dependency initializers and
storing the type's field values. This function may store to static constants
owned by the same scope. Earlier NIR files retain their encoded startup
order when built.

The tags follow the declaration order of the enumerations in [`src/neura-ir/ir.hpp`](https://github.com/Kayyo321/neura/blob/main/src/neura-ir/ir.hpp):

| Enumeration | Tags |
| --- | --- |
| Type kind | 0 void, 1 bool, 2 string, 3 integer, 4 float, 5 array, 6 list, 7 object, 8 type parameter, 9 module, 10 enum, 11 callable, 12 tuple (1.2) |
| Symbol kind | 0 file, 1 module, 2 class, 3 alias, 4 import, 5 field, 6 function, 7 parameter, 8 local, 9 global, 10 type parameter, 11 enum, 12 variant, 13 interface (1.3) |
| Operation | 0 initialize, 1 store, 2 load, 3 reference, 4 receiver, 5 field, 6 index, 7 collection, 8 integer, 9 float, 10 string, 11 boolean, 12 unary, 13 binary, 14 short circuit, 15 call, 16 virtual call, 17 external call, 18 base initialize, 19 allocate, 20 length, 21 append, 22 remove at, 23 return, 24 block, 25 branch, 26 loop, 27 break, 28 continue, 29 destroy, 30 discard. In 1.1, 31 convert, 32 collection version, 33 fail. In 1.2, 34 tuple, 35 project, 36 enum construct, 37 enum tag, 38 enum project, 39 comparison chain, 40 scoped value, 41 select value. In 1.3, 42 make closure, 43 call closure, 44 text length, 45 text byte at, 46 text slice, 47 text from UTF-8, 48 text equal, 49 reinterpret float, 50 trap message, 51 nil, 52 class hash, 53 class text, 54 class is, 55 class cast, 56 value equals, 57 value hash, 58 value text. In 1.4, 59 repeat collection. |

Adding an operation, symbol kind, or type kind changes these tables and requires a new format version. Region order is evaluation order. A short-circuit node keeps its right operand in `body`. A branch keeps its arms in `body` and `alternative`. A loop keeps its condition as its operand, its statements in `body`, and, in 1.1, its latch in `alternative`: statements that run after the body and on `continue`, before the condition. A convert node changes its operand to the node's type. A collection-version node reads a list's structural version as `uint64`. A fail statement ends the run with the failure its text names; the only one is `iteration`, a list modified during for-each.

A repeat-collection node takes a `uint64` length and an element value. It evaluates them in order and initializes every element of a new array or list of its result type to that value.

In 1.7, operation 60, `reference_place`, binds a primitive reference. Its symbol identifies the mutable referent, its single operand describes the place, and its result type names the compiler-provided reference layout for that primitive. A layout is an object type named by the resolved primitive with an `&` prefix. The compiler materializes the shared cells after linking, before specialization. References use ordinary object operations in the executable image.

Type names in `.nir` files are those lowering writes. Integer types are spelled by their sized names; files written before `int` and `byte` became aliases may still spell them `int` and `byte`, and the backend reads those as `int32` and `uint8`.

A tuple type stores its ordered element types in `arguments`. Tuple construction evaluates its operands in order. Tuple projection reads a field by its zero-based decimal `text` index. Enum construction names a variant in `symbol` and evaluates its operands in written order. Its `text` is empty for positional payloads, or a comma-separated permutation of field indices for named payloads. Enum tag reads the unsigned 32-bit discriminant. Enum projection names the variant and a decimal field index. Scoped values execute local statements before evaluating the final expression in `body`. Those statements can initialize only local bindings and can transfer control only within the scoped value. A select-value node evaluates its Boolean operand, then evaluates the sole expression in either `body` or `alternative`. A comparison chain initializes its subject in the first item in `body`, then evaluates its Boolean result in the second.

Structural callable types have an absent `symbol` and carry their parameter types followed by the result type. A closure constructor names its capture initializer in `symbol`, stores capture expressions in `operands`, and carries the capture layout's generic arguments in `type_arguments`. A call closure evaluates its function value, then its arguments. Specialization lowers both operations to allocation and virtual dispatch. Enum types carry the application arguments, while enum construct and project nodes continue to name the declaration's variant. Specialization remaps both the enum and variant symbols.

`nil` has a reference type and no operands. A class test takes one reference and names the target in its sole type argument. A class cast takes the reference and uses the node result type as the target. Value members use the first operand's type after specialization to select scalar, aggregate, or class behavior.

## Reading

The reader checks structure and leaves semantics to the [NeuraIR verifier](IrOptimization#neurair-verifier), which every build runs on the result. It rejects:

- a missing magic, an unsupported version, or bytes after the last body
- a truncated record, or a count larger than the remaining bytes could hold
- an unknown type kind, symbol kind, or operation tag, a bound flag other than 0 or 1, or reserved flag bits
- a symbol or file reference out of range, or an absent reference where one is required
- type or node nesting deeper than 1,024 levels, which bounds the recursion of the reader and of the passes that walk the program afterward

Paths in spans are recorded as the compiler saw them. They identify source locations for diagnostics and source maps. A standalone artifact build does not read those files. Source linking uses the recorded project file identities to detect repeated dependencies and compares embedded standard library definitions with the configured installation.

# NeuraIR file format

A `.nir` file holds a NeuraIR program as lowered, before optimization. `neura emit-ir <file.nra>... -o <file.nir>` writes one, and `neura build <file.nir> -o <file.nvr>` builds from it with the same verification, optimization, and conversion as a build from source. Source can also [import modules from it](lang/Modules.md#prebuilt-neurair-modules). The reader and writer are in [`src/neura-ir/nir_format.cpp`](../src/neura-ir/nir_format.cpp).

The file records every field of the in-memory program: symbols with their kinds, names, owners, inheritance and override links, types, parameters, generics, bounds, flags, and attributes, plus every body with its regions, operands, symbol references, literal text, type arguments, and source spans. A file that is written and then read yields a program equal to the original in every field. As a result, a build from `.nir` produces the same image bytes as a build from source. The human-readable listing printed by `neura emit-ir` without `-o` is a separate interface.

## Encoding

All minor versions use the conventions of [NVR](runtime/NVR.md#encoding-and-compatibility). Integers are fixed-width and little-endian. Records have no padding, and a `vector<T>` is a `u32` count followed by its elements. `text` is a byte vector. A symbol reference is a `u32` index into the symbol table, and `0xffffffff` marks an absent reference where the field is optional.

| Field | Type | Contents |
| --- | --- | --- |
| Magic | 4 bytes | ASCII `NEIR` |
| Major | u16 | 1 |
| Minor | u16 | 0 through 10 |
| Files | vector<text> | Source paths used by spans, in order of first use: symbols first (each symbol's span, then the spans in its attributes), then bodies depth first |
| Symbols | vector<Symbol> | The symbol table in symbol-index order |
| Bodies | vector<Body> | Bodies in program order |
| Static constants | vector<StaticConstant> | Present starting with 1.6 |

The reader accepts versions 1.0 through 1.10. The writer emits the earliest version that can encode the program. Version 1.1 adds loop latches and operation tags 31–33. Version 1.2 adds tuple types, enum payloads, aggregate and selection operations, comparison chains, and bitwise spellings on existing unary and binary nodes. Version 1.3 adds structural function types, generic enum applications, interfaces and intersection bounds, library and host bindings, nil references and class operations, universal value members, and text, float bit access, and message-trap operations. Version 1.4 adds repeated collection allocation, class static scopes and qualified static field access, and statements before a scoped value's result. Version 1.5 adds guarded static initialization using the existing operations. Version 1.6 preserves source linkage signatures and prepared static constants. Version 1.7 adds primitive reference layouts and operation tag 60. Version 1.8 adds host callbacks: a library function with a body and a host binding is a callback that the runtime calls back (NVR `CALB`). It also adds a trailing native library record after the static constants: a `u8` presence flag, then the target triple as text, the shared object as a byte count and bytes, and the symbol names as a counted list of text. A build copies the record into the image's `NATV` section. The reader rejects features newer than the file's declared version. Version 1.9 adds the task operations and the poll bit ([Revision 1.9](#revision-19-tasks)). Version 1.10 adds symbol attributes and deferred initialization ([Revision 1.10](#revision-110-symbol-attributes-and-deferred-initialization)).

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

In 1.6, each symbol ends with a `u8` linkage flag after the other optional fields. Bits 0–1 encode fixed arity (0), typed variadic (1), or untyped variadic (2). Bit 2 marks a user-defined object cast. Bit 3 marks an overload branch and adds a required symbol reference to the first branch. Bit 4 marks a singleton class and adds a required reference to its value. References follow in bit order. In 1.10, bit 5 marks a nonempty attribute vector after the references ([Revision 1.10](#revision-110-symbol-attributes-and-deferred-initialization)). A prepared static constant records the typed value available at compile time without running its class's initializer.

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

The tags follow the declaration order of the enumerations in [`src/neura-ir/ir.hpp`](../src/neura-ir/ir.hpp):

| Enumeration | Tags |
| --- | --- |
| Type kind | 0 void, 1 bool, 2 string, 3 integer, 4 float, 5 array, 6 list, 7 object, 8 type parameter, 9 module, 10 enum, 11 callable, 12 tuple (1.2) |
| Symbol kind | 0 file, 1 module, 2 class, 3 alias, 4 import, 5 field, 6 function, 7 parameter, 8 local, 9 global, 10 type parameter, 11 enum, 12 variant, 13 interface (1.3) |
| Operation | 0 initialize, 1 store, 2 load, 3 reference, 4 receiver, 5 field, 6 index, 7 collection, 8 integer, 9 float, 10 string, 11 boolean, 12 unary, 13 binary, 14 short circuit, 15 call, 16 virtual call, 17 external call, 18 base initialize, 19 allocate, 20 length, 21 append, 22 remove at, 23 return, 24 block, 25 branch, 26 loop, 27 break, 28 continue, 29 destroy, 30 discard. In 1.1, 31 convert, 32 collection version, 33 fail. In 1.2, 34 tuple, 35 project, 36 enum construct, 37 enum tag, 38 enum project, 39 comparison chain, 40 scoped value, 41 select value. In 1.3, 42 make closure, 43 call closure, 44 text length, 45 text byte at, 46 text slice, 47 text from UTF-8, 48 text equal, 49 reinterpret float, 50 trap message, 51 nil, 52 class hash, 53 class text, 54 class is, 55 class cast, 56 value equals, 57 value hash, 58 value text. In 1.4, 59 repeat collection. In 1.7, 60 reference place. In 1.9, 61 task scope, 62 spawn, 63 scheduler call. |

Adding an operation, symbol kind, or type kind changes these tables and requires a new format version. Region order is evaluation order. A short-circuit node keeps its right operand in `body`. A branch keeps its arms in `body` and `alternative`. A loop keeps its condition as its operand, its statements in `body`, and, in 1.1, its latch in `alternative`: statements that run after the body and on `continue`, before the condition. A convert node changes its operand to the node's type. A collection-version node reads a list's structural version as `uint64`. A fail statement ends the run with the failure its text names; the only one is `iteration`, a list modified during for-each.

A repeat-collection node takes a `uint64` length and an element value. It evaluates them in order and initializes every element of a new array or list of its result type to that value.

In 1.7, operation 60, `reference_place`, binds a primitive reference. Its symbol identifies the mutable referent, its single operand describes the place, and its result type names the compiler-provided reference layout for that primitive. A layout is an object type named by the resolved primitive with an `&` prefix. The compiler materializes the shared cells after linking, before specialization. References use ordinary object operations in the executable image.

Type names in `.nir` files are those lowering writes. Integer types are spelled by their sized names; files written before `int` and `byte` became aliases may still spell them `int` and `byte`, and the backend reads those as `int32` and `uint8`.

A tuple type stores its ordered element types in `arguments`. Tuple construction evaluates its operands in order. Tuple projection reads a field by its zero-based decimal `text` index. Enum construction names a variant in `symbol` and evaluates its operands in written order. Its `text` is empty for positional payloads, or a comma-separated permutation of field indices for named payloads. Enum tag reads the unsigned 32-bit discriminant. Enum projection names the variant and a decimal field index. Scoped values execute local statements before evaluating the final expression in `body`. Those statements can initialize only local bindings and can transfer control only within the scoped value. A select-value node evaluates its Boolean operand, then evaluates the sole expression in either `body` or `alternative`. A comparison chain initializes its subject in the first item in `body`, then evaluates its Boolean result in the second.

Structural callable types have an absent `symbol` and carry their parameter types followed by the result type. A closure constructor names its capture initializer in `symbol`, stores capture expressions in `operands`, and carries the capture layout's generic arguments in `type_arguments`. A call closure evaluates its function value, then its arguments. Specialization lowers both operations to allocation and virtual dispatch. Enum types carry the application arguments, while enum construct and project nodes continue to name the declaration's variant. Specialization remaps both the enum and variant symbols.

`nil` has a reference type and no operands. A class test takes one reference and names the target in its sole type argument. A class cast takes the reference and uses the node result type as the target. Value members use the first operand's type after specialization to select scalar, aggregate, or class behavior.

### Revision 1.9: tasks

Version 1.9 adds three operations for [concurrency](lang/Concurrency.md) and a poll flag. All three operations are in the table above.

| Operation | Region | Fields |
| --- | --- | --- |
| Task scope (61) | A statement. Its statements are in `body`; `operands` and `alternative` are empty. | `symbol` is the constant `uint64` local that receives the scope's identity for a `scope (name)` handle, or absent. `text` is empty. Every exit from `body`, including `return`, `break`, and `continue`, first waits for the tasks the scope owns. It can appear only in a function body. |
| Spawn (62) | An expression of type `uint64`, the task identity. | One operand: the function value of no arguments that the task runs (a make-closure node, or after specialization the allocation of its capture layout). `symbol`, `text`, `body`, and `type_arguments` are empty. It must be inside a task scope of the same body, and the function value must return a value. |
| Scheduler call (63) | An expression whose type is the operation's result. | `text` names the operation: `select`, `select+default`, or an entry of the operation table of `src/neura-ir/scheduler_ops.hpp` (for example `join`, `channel.send`, `scope.cancel`, or `transfer.check`). `symbol` is absent. `operands` are the arguments in evaluation order. `type_arguments` holds one type for `channel.open`, the channel's element type, and none otherwise. A select's operands are its arms: a `uint32` literal kind (0 receive, 1 send, 2 join, 3 timer wait) followed by that kind's operands. |

A **poll** is stored in bit 7 (`0x80`) of the operation byte of a loop, call, virtual call, or call-closure node; the reader rejects it on any other operation. It counts one language operation toward the running task's quantum, on a loop each time its back edge is taken and on a call when the call is made. Lowering sets it on every such node of a program that contains a spawn, and the flag is recomputed after specialization, so a module compiled alone carries none.

A file is written as 1.9 only when a body contains a task scope, a spawn, a scheduler call, or a poll; any other program keeps the earliest version that can encode it, and its bytes are unchanged. A 1.8 reader rejects a 1.9 file with `NeuraIR format 1.9 is not supported; this reader accepts 1.0 through 1.8`, and a 1.9 reader reads every earlier file. A build from a 1.9 file produces the same image bytes as a build from the source that wrote it, including the `TRNS` table.

### Revision 1.10: symbol attributes and deferred initialization

Version 1.10 adds two things that the features of Neura 0.2.0 need to keep in NeuraIR: typed attributes on symbols, and a declaration of a local without a value.

#### Symbol attributes

An attribute is a registered fact about one declaration: a `u16` identifier and a payload. [`src/neura-ir/attributes.def`](../src/neura-ir/attributes.def) is the registry, and [`src/neura-ir/attributes.hpp`](../src/neura-ir/attributes.hpp) is the API that attaches, finds, and checks attributes. A symbol keeps its attributes in the order they were attached, and the file keeps that order.

Bit 5 (`0x20`) of a symbol's linkage flag marks the attributes. They follow the overload and singleton references as a `vector<Attribute>` with at least one element.

| Record | Fields in order |
| --- | --- |
| Attribute | `id:u16`, `value:Value` |
| Value | `kind:u8`, then the payload of that kind |

| Value kind | Payload |
| --- | --- |
| 0 bool | `u8`, 0 or 1 |
| 1 signed | `u64`, a two's complement 64-bit integer |
| 2 unsigned | `u64` |
| 3 text | `text` |
| 4 symbol | `u32`, a required symbol reference |
| 5 type | `Type` |
| 6 span | `Span`, whose file index refers to the Files table |
| 7 list | `vector<Value>` |

Each registered attribute has a stable identifier, the symbol kinds that may carry it, a rule for whether one symbol may carry it more than once, and a payload schema. A schema uses these letters: `b` bool, `i` signed, `u` unsigned (`uN` also requires a value of at most `N`), `t` text, `s` symbol, `y` type, `p` span, `a` any literal (bool, signed, unsigned, or text), `*X` a list whose items all match `X`, and `[X Y ...]` a list of exactly those items in order.

| Id | Name | Carried by | Repeats | Schema | Payload |
| --- | --- | --- | --- | --- | --- |
| 1 | `tag` | every kind except file | yes | `[u65535 t *[t a] p]` | A source tag: its identifier in the tag registry, its name as written, its arguments as pairs of an argument name (empty when positional) and a literal value, and its span |
| 2 | `accessor.get` | field | no | `s` | The field's hidden getter function |
| 3 | `accessor.set` | field | no | `s` | The field's hidden setter function |
| 4 | `accessor.of` | function | no | `[s u1]` | On a hidden accessor function: the field it serves, and 0 for a getter or 1 for a setter |
| 5 | `binding.last` | local, field | no | `b` | A `last` binding. True when it is declared without an initializer |
| 6 | `class.anonymous` | class | no | `[b t p]` | An anonymous class: whether it is structural (fields only), its canonical structural signature, and the occurrence's span |
| 7 | `kernel.binding` | function | no | `[t u4294967295]` | A function implemented by a built-in C kernel: the kernel's name and ABI version |
| 8 | `inline.directive` | function | no | `u2` | The optimizer's inlining directive: 0 automatic, 1 always, 2 never |
| 9 | `synchronized` | function | no | `b` | A method that runs under its instance's guard |
| 10 | `class.shareable` | class | no | `b` | A class whose instances may be shared between tasks |

Identifier 0 is reserved, and identifiers are never reused. A new attribute takes the next free identifier and is added to this table. Registering one does not require a new format version: the version describes the encoding, and a reader that predates an identifier rejects the file as it rejects any unknown identifier.

The reader rejects an unknown identifier, an unknown value kind, a bool other than 0 or 1, an absent or out-of-range symbol reference, an attribute on a kind of symbol that the registry does not allow, a repeat of an attribute that does not repeat, and a payload that does not match its schema. The [NeuraIR verifier](IrOptimization.md#neurair-verifier) applies the same registry rules to programs built in memory. It also checks the following:

- Symbol references in payloads exist.
- A field's `accessor.get` or `accessor.set` names a function of the same owner, and that function's `accessor.of` names the field with the matching kind. The reverse also holds.
- `synchronized` marks a function owned by a class.
- A kernel binding names a kernel.

Linking a prebuilt module renumbers the symbol references, types, and span files in attributes, as it does for the symbol's own fields. Two definitions coalesce only when their attributes are equal. As with the 1.6 linkage fields, an embedded standard library definition without attributes still links against an installed one that has them. Specialization copies the attributes of every symbol it copies. A reference to a member of the generic class then names the instance's copy, which specialization creates if needed. A reference to a parameter or local of the copied function names its copy.

No registered attribute changes the executable image. A build from source and a build from the `.nir` file it wrote produce the same image bytes. Adding attributes to a program does not change its image.

#### Deferred initialization

An `initialize` node (operation 0) with no operand declares a local without a value. Its `symbol` is the local, its `text` is empty, and it has no regions. The local's slot exists from the declaration on, and holds no value until the first `store` to it. A read before that store traps with an uninitialized slot. The runtime tracks this state per frame, so it does not reset when the declaration runs again in a later loop iteration.

The verifier checks the structure:

- The target is a local, not a field or global. A deferred field has no `initialize` at all.
- The local carries `binding.last` with the value true. A binding with that value has no `initialize` with a value.
- A store to such a binding is allowed even when the binding is constant.
- A primitive reference cannot bind to such a binding.
- A comparison chain's subject is not deferred.
- The usual scoping rules apply: the declaration puts the local in scope, and a load outside that scope is rejected.

The verifier does no flow analysis. It does not check that a store precedes every read on every path, that the binding is assigned exactly once, that no assignment happens in a loop, or that no closure captures the binding before its assignment. The frontend's definite-assignment pass is responsible for these rules, and the runtime trap catches only a read before the first store in a frame. The optimizer treats the declaration as having no effect. It does not treat a load of such a binding as pure, because that load can trap.

#### Versioning

A file is written as 1.10 only when a symbol has an attribute or a body contains a deferred initialization. Every other program is written in the earliest version that can encode it, and its bytes are unchanged. A 1.9 reader rejects a 1.10 file with `NeuraIR format 1.10 is not supported; this reader accepts 1.0 through 1.9`. A 1.10 reader reads every earlier file. In a file that declares an earlier version, it rejects the attribute bit (`symbol attributes require NeuraIR format 1.10`) and deferred initialization (`deferred initialization requires NeuraIR format 1.10`).

## Reading

The reader checks structure and leaves semantics to the [NeuraIR verifier](IrOptimization.md#neurair-verifier), which every build runs on the result. It rejects:

- a missing magic, an unsupported version, or bytes after the last body
- a truncated record, or a count larger than the remaining bytes could hold
- an unknown type kind, symbol kind, operation tag, attribute identifier, or attribute value kind, a bound flag other than 0 or 1, or reserved flag bits
- an attribute that breaks a rule of the registry: its symbol kind, its repetition, or its payload schema
- a symbol or file reference out of range, or an absent reference where one is required
- type or node nesting deeper than 1,024 levels, which bounds the recursion of the reader and of the passes that walk the program afterward

Paths in spans are recorded as the compiler saw them. They identify source locations for diagnostics and source maps. A standalone artifact build does not read those files. Source linking uses the recorded project file identities to detect repeated dependencies and compares embedded standard library definitions with the configured installation.

# Neura 0.3.0: `Std.Reflect`, type descriptors, dynamic access and proxies

This note fixes the model of the 0.3.0 reflection library before it is implemented, as [the 0.3.0 baseline](Release030.md) requires: what the compiler emits for a program that reflects (type descriptors, field, variant, constructor and method tables, tags as attributes), the single NeuraIR operation that carries it, how the runtime exposes it through `Std.Reflect.Core`, `Type`, `Member`, `Attribute`, `Dynamic`, `Codec`, `Ffi` and `Proxy`, and what a program that does not reflect pays (nothing). It builds on [`typeof` and `Std.Reflect.Core.TypeInfo`](Expressions030.md#typeof), on [`any` and `match.is`](Matching030.md), on [records](Records030.md) and on [tags](../lang/Tags.md). The rows move to [the standard library page](../lang/Stdlib.md#reflection), [the NeuraIR format](../NirFormat.md) and [the language status](../lang/Status.md) in the change that implements them.

Status: *proposed* on 2026-11-18, implemented by autonomous agents the same day; every row awaits the owner's confirmation. The owner was away and asked for autonomous work toward 0.3.0. A row marked "implemented, proposed" is in the compiler or library and still awaits that confirmation; a row marked "gap" ships as a listed gap whose declarations follow the stub convention (`/// @stub` and a trap naming the reason).

The feature adds no syntax and no NVR operation. It adds one NeuraIR operation, `reflect` (op 75, NeuraIR 1.16), that only the trusted library emits and that the compiler expands after specialization into existing operations, as it expands `type of`. A program that does not use `Std.Reflect` loads none of its modules and builds the image it built before.

## Summary of the forms

<!-- example: fragment: the 0.3.0 reflection library; the tests under tests/stdlib/reflect-* run these forms -->
```neura
let reflect = import:Std.Reflect;
let codec = import:Std.Reflect.Codec.JsonBridge;

let Point = record (x: int32, y: int32);

let show = func(p: Point) -> void {
    let described = reflect.Type.of_value(p);
    for (let field in described.fields()) {
        print_line("${field.name}: ${field.type_name}");
    }
    let text = codec.encode(reflect.Dynamic.Variant.of(p));
    let back: Point = codec.decode_as<Point>(text);
};
```

## The descriptor model

| Rule | Status |
| --- | --- |
| The **reflection universe** of a program is computed on the linked, specialized program: the scalar types and `string`, every class the program allocates (generic templates and compiler classes left out, generic instances kept), every type named by a reflection site, and, closed under reachability, each field, payload, element, parameter, result, base and interface type of those. Each type of the universe has an **ordinal**: 0 is nil, 1 to 14 are the `any` tags (`bool`, the eight integers, `float16`, `bfloat16`, `float32`, `float64`, `string`), and the rest follow in a deterministic order (first reached in symbol order, then in declaration order of members). | implemented, proposed |
| Each ordinal has one **descriptor**: a string constant in the program, built by the compiler, in a line format that only `Std.Reflect` reads (see [Lowering](#lowering)). It gives the type's kind, name and qualified name as `typeof` spells them, an identity string, its layout in NVR value words, its base, interfaces and generic arguments, its fields, variants, constructors and methods with their parameter names and types (by ordinal), its flags (exported, `const`, `last`, accessors, record) and its tags. | implemented, proposed |
| A descriptor is execution data: it is a constant of the image, it is the same in an image without `NAME` or `SMAP`, and it is the same in an image built from a `.nir` module. It uses no symbol id, address or host width. | implemented, proposed |
| `TypeId` is the first 128 bits of SHA-256 of the identity string (`kind:qualified-name@declaring-file`), computed by `Std.Reflect.Core.TypeId` with `Std.Digest.Sha256`. It is the same for the same declaration in every build and every program; two types with one qualified name in two files differ by the file. | implemented, proposed |
| `MemoryLayout` describes the NVR representation: the number of value words, whether the value is inline (a tuple or payload enum), a reference, or an `any`, whether it is plain data (no reference, text or `any` word), and the bit width of a scalar. Neura has no byte addresses, so there is no byte offset or niche; `Std.Reflect.Ffi` gives C layouts. | implemented, proposed |
| `ReflectPolicy` is chosen per class by implementing one of the marker interfaces `Std.Reflect.Core.ReflectPolicy.Strip` or `Layout`; a class with neither is `Full`. `Strip` keeps kind, names, identity and layout, and dynamic access traps; `Layout` adds fields, variants, elements and constructors; `Full` adds methods and tags. Value types are always `Full`. Two markers on one class are `E1302`. | implemented, proposed |
| A program that never calls the reflection library emits no descriptor, no ordinal and no box class: its image is byte-identical to the image of the compiler before this feature. | implemented, proposed |

## Dynamic values

| Rule | Status |
| --- | --- |
| A dynamic value is an `any`. A value that `any` cannot hold (an array, list, tuple, enum, `Optional` and `Result` included, or a function value) is held in a **box**: a class the compiler synthesizes per such type of the universe, with one field. `Std.Reflect.Dynamic.Variant` pairs the `any` with the ordinal, so a box is never visible to the program. | implemented, proposed |
| `Variant.of(value)` packs any value of a type in the universe; `variant.get<T>()` unpacks it with a checked test: a different type traps with the expected and actual type names. Scope-bound values (`Task<T>`, `Scope`) and primitive references (`&T`) cannot be held: `E1301` at the call. | implemented, proposed |
| Reading a field, element or payload yields a `Variant`; writing one checks the value's ordinal against the member's type and traps on a mismatch, a `const` or `last` field, or a type whose policy keeps no members. A field with accessors is read and written through them, so hooks and accessor bodies run as for `object.field`. | implemented, proposed |
| `construct` calls a constructor (by index) of a class or record, builds an array, list or tuple from its elements, or an enum variant from its payload, with each argument checked as above. Construction is the only way to make a record (its fields are `last`). | implemented, proposed |
| `invoke` calls an instance method by index through ordinary dispatch, with each argument checked, and gives its result as a `Variant` (nil for `void`). Static functions, generic methods, variadic methods and methods whose parameter or result cannot be held are listed in the descriptor but not invocable: `invoke` traps naming the reason. | implemented, proposed |
| A dynamic operation on nil traps as a field read through nil does. | implemented, proposed |

## The packages

| Package | Contents | Status |
| --- | --- | --- |
| `Std.Reflect.Core` | `TypeId`, `TypeInfo` (the record of `typeof`, unchanged), `MemoryLayout`, `ReflectPolicy` markers, `TypeRegistry` (lookup of every universe type by ordinal, `TypeId` or qualified name; immutable, so it needs no lock), `Reflectable`. | implemented, proposed |
| `Std.Reflect.Type` | `TypeKind` (unchanged), `TypeDescriptor` with `kind`, names, `id`, `layout`, `base`, `interfaces`, `generic` (`GenericInfo`: origin name and argument descriptors), `fields`, `variants`, `methods`, `constructors`, `elements`; `of<T>()`, `of_value(value)`, `of_info(TypeInfo)`. `StructInfo`, `EnumInfo`, `TraitInfo` are views of a descriptor; `UnionInfo` and `VTable` do not apply (Neura has no unions or raw vtables) and are not provided. | implemented, proposed |
| `Std.Reflect.Member` | `FieldInfo`, `VariantInfo`, `PropertyInfo` (a field with accessors), `MethodInfo`, `ParamInfo`, `ConstructorInfo`, `Visibility`, `FieldFlags`, `ValueCursor` and `ValueCursorMut` (a `Variant` and a path of member indexes, read and written through `Dynamic`). | implemented, proposed |
| `Std.Reflect.Attribute` | `Attribute` (tag name, arguments as written), `AttributeTarget`, lookup on types, fields, variants, methods and parameters, and the builtin tags `SerialName`, `Skip` and `Sensitive` as the codec reads them. | implemented, proposed |
| `Std.Reflect.Dynamic` | `Variant`, `AnyRef`, `AnyMut` (a `Variant` with read or read-write access), `MethodInvoker`, `ConstructorInvoker`. | implemented, proposed |
| `Std.Reflect.Codec` | `ReflectVisitor`, `ReflectMutVisitor`, `WalkEngine` and `WalkControl` (depth-first walk with cycle detection by identity and skipping), `SchemaBuilder` (JSON Schema text), `JsonBridge` (a `Std.Encoding.Json.JsonValue` from any `Variant` and back). | implemented, proposed |
| `Std.Reflect.Ffi` | `CType`, `AbiLayout` and `CallingConvention` (C layouts and System V and AAPCS64 argument classification computed for a declared target, never the build host), `ForeignSymbol` and `CallFrame` over `Std.DynamicForeign`. | implemented, proposed |
| `Std.Reflect.Proxy` | `InvocationHandler`, `MethodCallContext`, `DynamicProxy`. | see [Proxies](#proxies) |

## Proxies

| Rule | Status |
| --- | --- |
| `DynamicProxy.create<I>(handler)` returns an `I` whose every method packs its receiver-free arguments into `Variant`s, calls `handler.invoke(context)` with a `MethodCallContext` (the interface descriptor, the method's index and `MethodInfo`, the arguments), and unpacks the result to the method's result type with a checked test. | gap: needs a class the compiler synthesizes per interface; `create` traps |
| `MethodCallContext` and `InvocationHandler` are usable without `create`: a hand-written class implementing `I` can forward to a handler with `Proxy.forward`. | implemented, proposed |

## Lowering

The trusted file `stdlib/std/reflect/core/intrinsics.nra` declares the module `Std.Reflect.Core.Intrinsics`, whose members are bodyless functions. Lowering turns a call of one of them into a `reflect` node whose text is the function's name, whose operands are the arguments and whose type arguments are the call's; a call from outside the standard library is `E1300`. `specialize()` expands every `reflect` node after `type of`, once types are concrete and the classes of the program exist, in `src/neura-ir/reflect.cpp`. The nodes and their expansions:

| `reflect` text | Operands | Result | Expansion | Status |
| --- | --- | --- | --- | --- |
| `ordinal` | a value | `int32` | the constant ordinal of a value type; for a reference a `ClassIs` chain over the universe classes, most-derived first, nil 0; for an `any` its tag, and for tag 0 the class chain, a box giving its content's ordinal | implemented, proposed |
| `ordinal_of` | type argument | `int32` | a constant | implemented, proposed |
| `table` | none | `string[]` | an array of the descriptor constants, indexed by ordinal | implemented, proposed |
| `pack` | a value | `any` | `AnyPack`, or a box allocated and filled | implemented, proposed |
| `unpack` | type argument, `any` | the type | a tag test and `AnyUnpack`, or a box test and its field read; otherwise a trap | implemented, proposed |
| `get`, `set` | `any`, ordinal, member index (, value) | `any` / `void` | a chain over the ordinal, then over the member: field read (or getter call) and pack; check, unpack and store (or setter call) | implemented, proposed |
| `length`, `element`, `variant` | `any`, ordinal (, index) | `int32` / `any` / `int32` | `Length`, `Index`, `Project`, `EnumTag`, `EnumProject` on the unboxed value | implemented, proposed |
| `construct` | ordinal, selector, `any[]` | `any` | `Allocate` with the chosen constructor, `Collection`, `Tuple` or `EnumConstruct` of the checked arguments | implemented, proposed |
| `invoke` | `any`, ordinal, method index, `any[]` | `any` | a `VirtualCall` (or `Call` of a final method) with checked arguments, its result packed | implemented, proposed |

Every expansion is made of operations that exist before NeuraIR 1.16, so the image needs no new runtime operation; storage checks, hook elaboration, task-safety and the optimizer see the expanded code. A `.nir` module keeps the `reflect` nodes as lowered, and the build that reads it expands them against the program it links.

The descriptor line format (one line per entry, fields separated by a tab, text fields escaped with `\\`, `\t` and `\n`):

```text
T kind name qualified identity words form plain bits policy base-ordinal generic-origin
G argument-ordinal
I interface-ordinal
E element-ordinal                    (array, list, tuple)
F name type-ordinal flags            (flags: e exported, c const, l last, g getter, s setter)
V name                               (then P name type-ordinal for each payload)
C index                              (then P name type-ordinal for each parameter)
M name result-ordinal flags          (flags: v invocable, f final, s static; then P lines)
@ tag argument...                    (attaches to the entry above it; argument is name=value)
```

## Diagnostics

| Code | When | Help | Status |
| --- | --- | --- | --- |
| E1300 | A call of a function of `Std.Reflect.Core.Intrinsics` outside the standard library, at the call | Use the public `Std.Reflect` modules | implemented, proposed |
| E1301 | A reflection operation on a type that cannot be held dynamically (scope-bound, a primitive reference, or a type parameter left unspecialized), at the call | Reflect a value that can be copied out of its scope | implemented, proposed |
| E1302 | A class that implements more than one `ReflectPolicy` marker, at the class | Keep one marker | implemented, proposed |

## Edge cases

1. A record, a class with a base, an enum with positional and named payloads, a tuple, an array, a list, `Optional<T>`, a function value, a generic instance such as `Box<int32>` and an interface each have a descriptor of the right kind, names and members.
2. `of_value` of a `Circle` held as `Shape` describes `Circle`; of nil it is the nil descriptor (ordinal 0).
3. A class the program never allocates is in the universe only when reached; `of<T>()` reaches it.
4. Field order is declaration order, base fields first.
5. A field with a getter is read through the getter: its body runs once per read.
6. Writing a `last` field, a `const` field, or a field of a `Strip` or `Layout` class through `set` traps; writing a value of another type traps naming both types.
7. `construct` of a record with the wrong argument count or types traps before any constructor runs.
8. `invoke` of a method with a wrong argument traps before the call; a method returning `void` gives a nil `Variant`.
9. A cycle of references is walked once per object by `WalkEngine`; the second visit reports `WalkControl.Cycle`.
10. JSON round trip: a record with integers, a float, a string, a bool, an array, an `Optional` and a nested record, and a class with a constructor, round-trip to equal values; `Sensitive` fields are written as `"<redacted>"`, `Skip` fields are left out, and `SerialName` renames.
11. Reflection works in an image built with `--no-source-map`, in an image without `NAME`, in an unoptimized build, and from a `.nir` module, with the same output.
12. Two builds of one program give byte-identical images (descriptors are deterministic).
13. A program that uses no reflection builds the same image as before (image identity over the corpus).
14. A call of an intrinsic from a user file is E1300; `Variant.of` of a `Task` is E1301; a class implementing `Strip` and `Layout` is E1302.
15. `Ffi.AbiLayout` of a C struct `{ char; int; double }` on `x86_64-linux-gnu` has size 16, alignment 8 and offsets 0, 4, 8, and the same on `aarch64-linux-gnu`; a struct of two doubles is class SSE in System V.

## Order of work

| Step | Content | Status |
| --- | --- | --- |
| 1 | This note. | implemented, proposed |
| 2 | Op 75, the universe and the expansion; `Core`, `Type`, `Member`. | implemented, proposed |
| 3 | `Attribute`, `Dynamic`. | implemented, proposed |
| 4 | `Codec` with the JSON bridge; `Proxy`. | implemented, proposed |
| 5 | `Ffi`. | implemented, proposed |
| 6 | Docs, status, changelog, wiki. | implemented, proposed |

## Gate and acceptance

The stdlib tests under `tests/stdlib/reflect-*` pass optimized and unoptimized; the reflection programs give the same output in every execution mode (`default`, `--tier=0`, `--jit=off`, each `NEURA_TIER3` mode, `NEURA_TIER_CHECK=2`), from source, from `.nir` and stripped; the image-identity corpus reports no changed image; the doc checks pass.

## Migration

No source changes: no new word, operator or tag. NeuraIR 1.16 adds op 75; a 1.15 reader rejects a module that contains it, and modules without it are written as before. The tags `SerialName`, `Skip` and `Sensitive` change from reserved to informational: a program that wrote them was rejected (E0908) before, so no accepted program changes meaning.

## Decisions taken without the owner

- **Descriptors are generated, not stored in a new section.** The spec places metadata in read-only binary data; Neura's equivalent is string constants that the compiler emits only for reflecting programs, which keeps them in stripped images, needs no image section or loader change, and leaves every other image unchanged.
- **"Zero allocation" is adapted.** The NVM allocates every object and string, so descriptors are parsed from their constant on first use and cached per program run; a `ValueCursor` is a `Variant` and an index path, not a raw pointer. Nothing claims zero allocation.
- **Linker metadata stripping (COMDAT) is replaced by closed-world emission.** Only types in the universe get descriptors and dynamic code, and only in programs that reflect; `ReflectPolicy` trims a class further.
- **Raw-pointer cursors, unions, vtables and arena construction do not apply.** Neura has no addresses, unions or user-visible vtables; the descriptors give member indexes and the dynamic layer goes through checked operations.
- **`TypeId` is SHA-256 based, computed at run time from compile-time constants.** It is deterministic and the same across builds; it is not a security boundary.
- **Builtin tags.** `SerialName`, `Skip` and `Sensitive` become informational tags read by `Std.Reflect.Codec`; `derive Json.Codec` still ignores them, and the tags' text says that only reflection-driven codecs read them. `Range` and `SerialDefault` stay reserved: a range is a promise the compiler would have to check.
- **Policy markers are interfaces, not tags,** so that no new tag id or syntax is needed and the marker is visible in the type.
- **Proxies** need a synthesized class per interface; if that is not finished for the release, `DynamicProxy.create` follows the stub convention and `Proxy.forward` covers hand-written forwarding classes.

## Decisions taken in the implementation

(Filled in by the implementing commits.)

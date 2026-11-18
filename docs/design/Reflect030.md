# Neura 0.3.0: `Std.Reflect`, type descriptors, dynamic access and proxies

This note fixes the model of the 0.3.0 reflection library before it is implemented, as [the 0.3.0 baseline](Release030.md) requires: what the compiler emits for a program that reflects (type descriptors, field, variant, constructor and method tables, tags as attributes), the single NeuraIR operation that carries it, how the runtime exposes it through `Std.Reflect.Core`, `Type`, `Member`, `Attribute`, `Dynamic`, `Codec`, `Ffi` and `Proxy`, and what a program that does not reflect pays (nothing). It builds on [`typeof` and `Std.Reflect.Core.TypeInfo`](Expressions030.md#typeof), on [`any` and `match.is`](Matching030.md), on [records](Records030.md) and on [tags](../lang/Tags.md). The rows move to [the standard library page](../lang/Stdlib.md#22-stdreflect), [the NeuraIR format](../NirFormat.md) and [the language status](../lang/Status.md) in the change that implements them.

Status: *proposed* on 2026-11-18, implemented by autonomous agents the same day; every row awaits the owner's confirmation. The owner was away and asked for autonomous work toward 0.3.0. A row marked "implemented, proposed" is in the compiler or library and still awaits that confirmation; a row marked "gap" ships as a listed gap whose declarations follow the stub convention (`/// @stub` and a trap naming the reason).

The feature adds no syntax, no NVR operation and no NeuraIR format revision. The trusted library calls bodyless functions of one internal module; inside `specialize()` the compiler turns each such call into an in-memory operation, `reflect` (IrOp 75), before specialization and expands it into existing operations after it, as it expands `type of`. The operation is never written to `.nir`: a `.nir` module keeps the ordinary calls, and the build that reads it expands them against the program it links. A program that does not use `Std.Reflect` loads none of its modules and builds the image it built before.

## Summary of the forms

<!-- example: check -->
```neura
let console = import:Std.Io.Console;
let bridge = import:Std.Reflect.Codec.JsonBridge;
let types = import:Std.Reflect.Type.TypeDescriptor;
let variants = import:Std.Reflect.Dynamic.Variant;

let Point = record (x: int32, y: int32);

let show = func(p: Point) -> Point {
    for (let field in types.of_value(p).fields) {
        console.print_line(field.name .. ": " .. field.type_name());
    }
    let text = bridge.encode(variants.of(p));
    return bridge.decode_as<Point>(text);
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
| `ReflectPolicy` is chosen per class by implementing one of the marker interfaces `Std.Reflect.Core.ReflectPolicy.Strip` or `Layout` (directly or through a base); a class with neither is `Full`. `Strip` keeps kind, names, identity and layout, and field access, construction and invocation trap; `Layout` adds fields (with their tags), constructors and writes; `Full` adds methods, invocation and method tags. The type's own tags are kept under every policy. Value types are always `Full`. Two markers on one class are `E1302`. | implemented, proposed |
| A program that never calls the reflection library emits no descriptor, no ordinal and no box class: its image is byte-identical to the image of the compiler before this feature. | implemented, proposed |

## Dynamic values

| Rule | Status |
| --- | --- |
| A dynamic value is an `any`. A value that `any` cannot hold (a list, tuple or enum, `Optional` and `Result` included) is held in a **box**: a class the compiler synthesizes per such type of the universe, with one field. `Std.Reflect.Dynamic.Variant` pairs the `any` with the ordinal, so a box is never visible to the program. An array lives in its region (Storage030) and cannot be boxed: it, and a tuple, list or enum containing one, has a descriptor but no dynamic value (`E1301` where the program packs one), and fields of such types are skipped by `get`, `set` and the codec. Function values are class references after specialization and are held directly. | implemented, proposed |
| `Variant.of(value)` packs any value of a type in the universe; `variant.get<T>()` unpacks it with a checked test: a different type traps with the expected and actual type names. Arrays, scope-bound values (`Task<T>`, `Scope`) and primitive references (`&T`) cannot be held: `E1301` at the call that instantiates the library function. Nothing is converted: an `int64` is not an `int32`. | implemented, proposed |
| Reading a field, element or payload yields a `Variant`; writing one checks the value's ordinal against the member's type and traps on a mismatch, a `const` or `last` field, or a type whose policy keeps no members. A field with accessors is read and written through them, so hooks and accessor bodies run as for `object.field`. | implemented, proposed |
| `construct` calls a constructor (by index) of a class or record, builds an array, list or tuple from its elements, or an enum variant from its payload, with each argument checked as above. Construction is the only way to make a record (its fields are `last`). | implemented, proposed |
| `invoke` calls an instance method by index through ordinary dispatch, with each argument checked, and gives its result as a `Variant` (nil for `void`). Generic and variadic methods are not listed; methods whose parameter or result cannot be held are listed with `invocable` false, and `invoke` traps naming them. The root methods (`to_string`, `equals`, `hash_code`) and static functions are not listed. | implemented, proposed |
| A dynamic operation on nil traps as a field read through nil does. | implemented, proposed |
| Dynamic access reaches exported members only: reading or writing an unexported field, calling an unexported method or constructor traps naming it, and the codec, the walker and the schema builder leave unexported fields out. Descriptors list every member with its `exported` flag. | implemented, proposed |
| The descriptor of a value agrees with `typeof`: the dynamic class of a reference the universe names, and otherwise the static type of the operand (`class` for an `any`), so a live object is never described as nil; a proxy is described as its interface. | implemented, proposed |
| Reflection keeps a per-run cache of parsed descriptors in module state, so a spawned task cannot call it (E0601 names the cache). | implemented, proposed (limit) |

## The packages

| Package | Contents | Status |
| --- | --- | --- |
| `Std.Reflect.Core` | `TypeId`, `TypeInfo` (the record of `typeof`, unchanged), `MemoryLayout`, `ReflectPolicy` markers, `TypeRegistry` (lookup of every universe type by ordinal, `TypeId` or qualified name; the table is a constant, and the per-run parse cache is the only state), `Reflectable` (every type is reflectable without declaring anything, so the contract is a module of operations: `descriptor<T>()`, `descriptor_of(value)`, `cursor(value)`, `cursor_mut(value, path)`). | implemented, proposed |
| `Std.Reflect.Type` | `TypeKind` (unchanged) and `TypeDescriptor` with `kind`, names, `id`, `layout`, `policy`, `base()`, `interfaces`, `generic_origin` and `generic_arguments` (the spec's `GenericInfo`, as names), `fields`, `variants`, `methods`, `constructors`, `elements`, `attributes`, `derives_from`; `of<T>()`, `of_value(value)`, `of_ordinal(n)`, `of_info(TypeInfo)`. The spec's `StructInfo`, `EnumInfo` and `TraitInfo` are the one `TypeDescriptor` of a class or record, an enum, or an interface; `UnionInfo` and `VTable` do not apply (Neura has no unions or user-visible vtables). | implemented, proposed |
| `Std.Reflect.Member` | `Members` (`FieldInfo`, `VariantInfo`, `MethodInfo`, `ParamInfo`, `ConstructorInfo`, `Visibility`, `FieldFlags`; a property is a `FieldInfo` whose `is_property()` holds, read and written through its accessors), and `ValueCursor` (`ValueCursor`, `ValueCursorMut`: a `Variant` and a path of member indexes, read and written through `Dynamic`). | implemented, proposed |
| `Std.Reflect.Attribute` | `Attributes`: `Attribute` (tag name, arguments as written), `AttributeTarget`, the tags of a type (`of_type`) and of a field (`of_field`); variants, methods and parameters carry theirs in `attributes`; `find`, `has`, and the builtin tags `SerialName`, `Skip` and `Sensitive` as the codec reads them (`serial_name`, `skipped`, `sensitive`). | implemented, proposed |
| `Std.Reflect.Dynamic` | `Variant` (`of`, `get<T>`, `is_type<T>`, `field`, `set_field`, `length`, `element`, `variant_index`, `invoke`) and `construct`. The spec's `AnyRef` and `AnyMut` are a `Variant` (Neura has no lifetime-bound references), and `MethodInvoker` and `ConstructorInvoker` are `Variant.invoke` and `construct`. | implemented, proposed |
| `Std.Reflect.Codec` | `Walk` (`ReflectVisitor`, `ReflectMutVisitor`, `WalkEngine`, `WalkControl`: a depth-first walk with cycle detection by identity and skipping), `SchemaBuilder` (JSON Schema 2020-12, proto3 messages, SQL `CREATE TABLE`), `JsonBridge` (a `Std.Encoding.Json.JsonValue` from any `Variant` and back). | implemented, proposed |
| `Std.Reflect.Ffi` | `Abi` (`Target`, `CType`, `CStruct`, `AbiLayout`, `CallingConvention`, `ArgumentClass`: C layouts and argument classes of System V x86-64, AAPCS64, Windows x64 and wasm32, computed for a declared target triple, never the build host) and `ForeignCall` (`ForeignSymbol`, `CallFrame` over `Std.DynamicForeign`). | implemented, proposed |
| `Std.Reflect.Proxy` | `DynamicProxy` (`create`, `forward`, `InvocationHandler`, `MethodCallContext`). | implemented, proposed |

## Proxies

| Rule | Status |
| --- | --- |
| `DynamicProxy.create<I>(handler)` returns an `I` whose every method packs its arguments into `Variant`s, calls `handler.invoke(context)` with a `MethodCallContext` (the interface descriptor, the `MethodInfo`, the arguments), and unpacks the result to the method's result type with a checked test. The compiler synthesizes one proxy class per interface the program proxies. | implemented, proposed |
| `I` must be an interface whose requirements, including those of the interfaces it extends, are non-generic, non-variadic instance methods; anything else is `E1300` at the call. A method whose parameter or result cannot be held traps when called. | implemented, proposed |
| `DynamicProxy.forward(target, context)` calls the same method on a reflected object, for handlers that wrap a real implementation. | implemented, proposed |

## Lowering

The trusted file `stdlib/std/reflect/internal/intrinsics.nra` declares the module `Std.Reflect.Internal.Intrinsics`, whose members are bodyless functions; its `Internal` path component keeps programs outside the standard library from importing it (E0911). At the start of `specialize()`, `prepare_reflection` turns each call of one of them into a `reflect` node whose text is the function's name, whose operands are the arguments and whose type arguments are the call's, so specialization makes its types concrete; after specialization and `type of`, `expand_reflection` (`src/neura-ir/reflect.cpp`) replaces every node. The nodes and their expansions:

| `reflect` text | Operands | Result | Expansion | Status |
| --- | --- | --- | --- | --- |
| `ordinal` | a value | `int32` | the constant ordinal of a value type; for a reference a `ClassIs` chain over the universe classes, most-derived first, nil 0; for an `any` its tag, and for tag 0 the class chain, a box giving its content's ordinal | implemented, proposed |
| `ordinal_of` | type argument | `int32` | a constant | implemented, proposed |
| `table` | none | `string[list]` | a list of the descriptor constants, indexed by ordinal | implemented, proposed |
| `pack` | a value | `any` | `AnyPack`, or a box allocated and filled | implemented, proposed |
| `unpack` | type argument, `any` | the type | a tag test and `AnyUnpack`, or a box test and its field read; otherwise a trap | implemented, proposed |
| `get`, `set` | `any`, ordinal, member index (, value) | `any` / `void` | a chain over the ordinal, then over the member: field read (or getter call) and pack; check, unpack and store (or setter call) | implemented, proposed |
| `length`, `element`, `variant` | `any`, ordinal (, index) | `int32` / `any` / `int32` | `Length`, `Index`, `Project`, `EnumTag`, `EnumProject` on the unboxed value | implemented, proposed |
| `construct` | ordinal, selector, `any[list]` | `any` | `Allocate` with the chosen constructor, `Collection`, `Tuple` or `EnumConstruct` of the checked arguments | implemented, proposed |
| `invoke` | `any`, ordinal, method index, `any[list]` | `any` | a `VirtualCall` with checked arguments, its result packed | implemented, proposed |
| `proxy` | type argument, handler | the interface | an allocation of the synthesized proxy class, whose methods call `Std.Reflect.Internal.Model.proxy_dispatch` | implemented, proposed |

The dynamic operations are synthesized functions (`$reflect_get`, `$reflect_set`, ...) that select the type by a balanced tree of ordinal tests and the member by index, so a site is one call. Every expansion is made of existing operations, so the image needs no new runtime operation; storage checks, hook elaboration, task-safety and the optimizer see the expanded code.

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
| E1300 | `DynamicProxy.create<I>` where `I` is not an interface, or requires a property, has a static requirement, or a generic or variadic method, at the call | Proxy an interface of plain methods | implemented, proposed |
| E1301 | Packing or unpacking a type a dynamic value cannot hold (an array or a value containing one, a scope-bound value, a primitive reference), at the call that instantiates the library function | Copy an array into a list | implemented, proposed |
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
10. JSON round trip: a record with integers, a float, a string, a bool, a tuple, an `Optional` and a nested record, and a class with a list, an enum and a no-argument constructor, round-trip to equal values; `Sensitive` fields are written as `"<redacted>"`, `Skip` and unexported fields are left out, and `SerialName` renames, also under the `Layout` policy. A record with a `Sensitive` or `Skip` component is not rebuilt: decoding returns `reflect.json.missing`.
11. Reflection works in an image built with `--no-source-map`, in an image without `NAME`, in an unoptimized build, and from a `.nir` module, with the same output.
12. Two builds of one program give byte-identical images (descriptors are deterministic).
13. A program that uses no reflection builds the same image as before (image identity over the corpus).
14. `create` of a class or of an interface with a property is E1300; `Variant.of` of an `int32[]` is E1301; a class implementing `Strip` and `Layout` is E1302; descriptors of arrays and proxies of plain interfaces are accepted.
15. `Ffi.AbiLayout` of a C struct `{ char; int; double }` on `x86_64-linux-gnu` has size 16, alignment 8 and offsets 0, 4, 8, and the same on `aarch64-linux-gnu`; a struct of two doubles is class SSE in System V.

## Order of work

| Step | Content | Status |
| --- | --- | --- |
| 1 | This note. | implemented, proposed |
| 2 | IrOp 75, the universe and the expansion; `Core`, `Type`, `Member`. | implemented, proposed |
| 3 | `Attribute`, `Dynamic`. | implemented, proposed |
| 4 | `Codec` with the JSON bridge; `Proxy`. | implemented, proposed |
| 5 | `Ffi`. | implemented, proposed |
| 6 | Docs, status, changelog, wiki. | implemented, proposed |

## Gate and acceptance

The stdlib tests under `tests/stdlib/reflect-*` pass optimized and unoptimized; the reflection programs give the same output in every execution mode (`default`, `--tier=0`, `--jit=off`, each `NEURA_TIER3` mode, `NEURA_TIER_CHECK=2`), from source, from `.nir` and stripped; the image-identity corpus reports no changed image; the doc checks pass.

## Migration

No source changes: no new word, operator or tag, and no NeuraIR or NVR revision. The tags `SerialName`, `Skip` and `Sensitive` change from reserved to informational: a program that wrote them was rejected (E0908) before, so no accepted program changes meaning.

## Decisions taken without the owner

- **Descriptors are generated, not stored in a new section.** The spec places metadata in read-only binary data; Neura's equivalent is string constants that the compiler emits only for reflecting programs, which keeps them in stripped images, needs no image section or loader change, and leaves every other image unchanged.
- **"Zero allocation" is adapted.** The NVM allocates every object and string, so descriptors are parsed from their constant on first use and cached per program run; a `ValueCursor` is a `Variant` and an index path, not a raw pointer. Nothing claims zero allocation.
- **Linker metadata stripping (COMDAT) is replaced by closed-world emission.** Only types in the universe get descriptors and dynamic code, and only in programs that reflect; `ReflectPolicy` trims a class further.
- **Raw-pointer cursors, unions, vtables and arena construction do not apply.** Neura has no addresses, unions or user-visible vtables; the descriptors give member indexes and the dynamic layer goes through checked operations.
- **`TypeId` is SHA-256 based, computed at run time from compile-time constants.** It is deterministic and the same across builds; it is not a security boundary.
- **Builtin tags.** `SerialName`, `Skip` and `Sensitive` become informational tags read by `Std.Reflect.Codec`; `derive Json.Codec` still ignores them, and the tags' text says that only reflection-driven codecs read them. `Range` and `SerialDefault` stay reserved: a range is a promise the compiler would have to check.
- **Policy markers are interfaces, not tags,** so that no new tag id or syntax is needed and the marker is visible in the type.
- **Proxies** are classes the compiler synthesizes per proxied interface after specialization; their dynamic type is reported as `class`.

## Decisions taken in the implementation

- **No NeuraIR revision.** The note first gave `reflect` its own `.nir` tag and minor version. Making the node only inside `specialize()` from calls of bodyless library functions gives the same expansion without a format change: a `.nir` module holds the calls, and images built from source and from `.nir` are byte-identical (tests/reflect/reflect.py).
- **E1300 is the proxy rejection.** A user call of an intrinsic is already E0911 (`#Internal`), so the code first reserved for it names the interfaces a proxy cannot implement.
- **E1301 at the instantiating call.** The check runs in the specializer, where the origin of a generic instantiation is known, so the error points at the user's `Variant.of(...)` rather than into the library.
- **Arrays have no dynamic value.** Arrays are region values (Storage030); a heap box cannot hold one. Descriptors still describe array types, and the codec skips array-typed fields.
- **Integer and float values are not converted.** `set`, `construct`, `invoke` and `get<T>` need the exact type, as `match.is` does; the JSON bridge parses numbers for the field's own width with that width's `parse`.
- **Descriptor text is parsed on first use** of each ordinal and cached for the run; `TypeId` is computed when a descriptor is parsed. The cache is module state, so reflection is not task-safe (E0601 for a spawned call that reaches it); dropping the cache would parse and hash a descriptor on every use.
- **Tags the codec reads.** `#Sensitive` on a field or a class (every value of the class is written as the placeholder and is not decoded), `#SerialName` on a field, a variant, and a class (its schema name), `#Skip` on a field. A type's own tags are kept under every policy, so the codec reads them for `Strip` and `Layout` classes too. Fields a `Variant` cannot hold (arrays, scope-bound values, primitive references) carry `dynamic` false and are left out of encoding, walking and schemas; an `any` field is written and not read back.
- **Only exported members are dynamic.** Reflection would otherwise read private fields of any class, the standard library's included. Descriptors still list unexported members with their flag.
- **`typeof`'s static-type fallback** (Expressions030, ec829ffd) is followed: `$reflect_ordinal` takes the operand's static ordinal as the answer for a live object of a class outside the universe, and `class` is always in the universe.
- **`Reflectable` is a module, not an interface.** The compiler describes every type, so a marker a class must implement would only restrict reflection; the module gives the contract's operations.

# Calling C code

Neura reaches native code two ways: `Std.DynamicForeign`, modeled on Java's Foreign Function and Memory API, loads shared libraries at run time; `import:c("header.h", "lib.a")` reads a C header at compile time and links static libraries into the image. Native code runs in the runtime's process outside Neura's safety and determinism guarantees, and a program that uses it cannot also use tasks. Specification: [Foreign interop](https://github.com/Kayyo321/neura/blob/main/docs/lang/ForeignInterop.md).

## C header imports

<!-- example: check -->
```neura
let console = import:Std.Io.Console;
let clib = import:c("stdlib.h");             // functions the C library already provides

let show = func {
    console.print_line(clib.abs(-9).to_string());
};
```

<!-- example: fragment: imports mathops.h and libmathops.a, which this page does not provide -->
```neura
let mathops = import:c("mathops.h", "libmathops.a");    // header plus static libraries

let sum = mathops.add(5, 3);                 // int add(int, int)
let point = mathops.make_point(3, 4);        // struct returned by value: a class
let name = mathops.MATHOPS_NAME;             // #define MATHOPS_NAME "mathops"
```

- The header is found beside the importing file, in `-I` roots, in `--c-include` directories, and in the system include directories. Libraries must be static (`.a`, `.o`) and built with `-fPIC`; shared libraries go through `SymbolLookup`.
- Mapping: C integer and float types become the same-size Neura types; any pointer is a `MemorySegment` (a `const char*` parameter also accepts `string`); a struct is a class with its fields, static `layout()`, `of(...)`, `read(segment)`, and `write_to(segment)`; enumerators and constant macros become `export const`; globals get `get_name()`/`set_name(v)`; variadic functions take untyped extra arguments.
- Not mapped (and an error naming the reason if used): function-like macros, bit-fields, unions by value, `long double`, `_Complex`, 128-bit integers, packed structs, flexible array members.
- C names are kept; a name that is a Neura keyword gets a trailing `_`.
- Compiling C imports needs libclang 14+ and its builtin headers (`neura doctor` checks), and linking needs a C compiler (`--cc`, `$CC`, or `cc`). Running the image needs neither.
- `neura c-bindings header.h` prints the generated module, which is the best way to learn the exact names and types.

## Std.DynamicForeign

<!-- example: check -->
```neura
let console = import:Std.Io.Console;
let arenas = import:Std.DynamicForeign.Arena;
let descriptors = import:Std.DynamicForeign.FunctionDescriptor;
let layouts = import:Std.DynamicForeign.ValueLayout;
let linkers = import:Std.DynamicForeign.Linker;
let lookups = import:Std.DynamicForeign.SymbolLookup;
let optional = import:Std.Core.Optional;
let result = import:Std.Core.Result;
let segments = import:Std.DynamicForeign.MemorySegment;
let values = import:Std.DynamicForeign.ForeignValue;

let string_length = func(text: string) -> int64 {
    let arena = arenas.of_confined();
    let libc = result.expect<lookups.SymbolLookup>(lookups.library_lookup("libc.so.6", arena), "load libc");
    let symbol = optional.expect<segments.MemorySegment>(libc.find("strlen"), "find strlen");
    let strlen = linkers.native_linker().downcall_handle(symbol, descriptors.of(layouts.Int64, layouts.Address));
    let length = values.as_int64(strlen.invoke(text));    // a string passes as a NUL-terminated copy
    arena.close();
    return length;
};
```

- `Arena.of_confined()` owns native memory and loaded libraries until `close()`; `Arena.global()` lives for the run. Allocation (`allocate`, `allocate_utf8`, `allocate_array`) is zeroed and bounds-checked.
- `MemorySegment` reads and writes are checked against the segment's size and its arena; a raw pointer has size 0 until `reinterpret(size)`.
- `FunctionDescriptor.of(result, args...)` / `of_void(args...)` with `ValueLayout` (`Bool`, `Int8`..`UInt64`, `Float32`, `Float64`, `Address`) or `Linker.canonical_layout("long")`. `MemoryLayout.struct_layout` lays out structs as C does.
- `MethodHandle.invoke(args...)` returns a `ForeignValue`; read it with `as_int32`, `as_int64`, `as_float64`, `as_segment`. Variadic C functions take `downcall_handle(symbol, descriptor, first_variadic)`.
- `Linker.upcall_stub(target, descriptor, arena)` turns a `func(ForeignValue[list]) -> ForeignValue` into a C function pointer (for callbacks such as `qsort` comparators).
- Library names are platform-specific (`libc.so.6`, `libm.so.6` on Linux; `/usr/lib/libSystem.B.dylib` on macOS).
- With `--deny-native` the first native operation traps (status 134). `--record`/`--replay` capture native calls.

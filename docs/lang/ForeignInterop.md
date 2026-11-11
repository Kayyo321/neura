# Foreign interop

Neura programs can call native C code in two ways. `Std.DynamicForeign`, a standard library modeled on Java's Foreign Function and Memory API, loads shared libraries at run time. A [C import](#c-imports) reads a C header at compile time and links static libraries into the image. Native code runs in the [Neura Virtual Machine](../runtime/NVM.md)'s process as a host service. It never becomes part of the circuit. Every native operation crosses the runtime boundary as a host request. The native group is granted by default, like every other host service, and `--deny-native` (or `--deny-all`) turns it off.

[`examples/foreign/native.nra`](../../examples/foreign/native.nra) uses both.

## Safety and determinism

Native code is outside Neura's guarantees. A native function can read or write any memory in the runner's process, crash the process, or behave differently from run to run. Because the native group is granted by default, any program you run with `neura run` or `neura test`, and any executable built with `neura build --native` without `--deny-native`, can reach native code with no option at all. Run an image you do not trust with `--deny-native` or `--deny-all`. `--deny-native` is the counterpart of Java's restriction on `--enable-native-access`; allow native access only to programs whose native libraries you trust.

Within those limits, the library checks what it can before native code runs:

- **Memory access.** Every read and write through a `MemorySegment` checks that its arena is still open and that the bytes lie inside the segment. A violation traps with `invalid state: arena closed` or `invalid argument: out of bounds`, before any native memory is touched.
- **Calls.** A call converts each argument to the layout its `FunctionDescriptor` gives it. An argument that does not fit, a wrong argument count, or a variadic argument that C's default argument promotions would widen traps with `invalid argument: ...`.
- **Denied capability.** With `--deny-native` (or `--deny-all`), the first native operation traps with `invalid state: native access denied: the native capability is not granted` (status 134, `ExitCode.Abort`). The message is the library's fixed text. Creating an arena or a layout needs no capability.
- **Native failures.** A crash inside native code ends the process with the signal's status, such as 139 for a segmentation fault. The NVM cannot report a source location for it.

Native results enter through the host boundary like file contents or clock readings. `neura run --record` stores every native request and its result, and `--replay` reproduces the run without the capability or the library. Addresses, and anything a program derives from them, can differ between runs. A program whose output depends only on the values native functions return is deterministic under replay.

## Std.DynamicForeign

| Module | Java counterpart | Contents |
| --- | --- | --- |
| `Arena` | `Arena` | `of_confined()`, `of_shared()`, and `global()`; `allocate(size, alignment)`, `allocate(layout)`, `allocate_array`, `allocate_utf8`, `allocate_bytes`, and `close()` |
| `MemorySegment` | `MemorySegment` | `address()`, `byte_size()`, `reinterpret(size)`, `as_slice`, typed `get_*` and `set_*` for each scalar, `get_utf8` and `set_utf8`, `to_bytes`, `copy_from`, `fill`; `null_segment()` and `of_address(address)` |
| `MemoryLayout` | `MemoryLayout` | `value(carrier)`, `struct_layout`, `union_layout`, `sequence_layout`, `padding_layout`; `byte_size`, `byte_alignment`, `with_name`, `byte_offset(path...)`, `element_offset` |
| `ValueLayout` | `ValueLayout` | `Bool`, `Int8` through `UInt64`, `Float32`, `Float64`, and `Address` |
| `FunctionDescriptor` | `FunctionDescriptor` | `of(result, arguments...)` and `of_void(arguments...)` |
| `SymbolLookup` | `SymbolLookup` | `library_lookup(path, arena) → Result<SymbolLookup>`, `find(name) → Optional<MemorySegment>`, `find_or_trap`, `or_else`, `default_lookup()`, `embedded()` |
| `Linker` | `Linker` | `native_linker()`, `downcall_handle(symbol, descriptor)` and its variadic overload, `upcall_stub(target, descriptor, arena)`, `default_lookup()`, `canonical_layout(name)` |
| `MethodHandle` | `MethodHandle` | `invoke(args...)`, `invoke_with(arena, args...)`, `invoke_values(values)` |
| `ForeignValue` | | The tagged argument and result enum, and `as_int32`, `as_int64`, `as_float64`, `as_segment`, `describe` |

The Java example that loads `libmathops.so`, finds `add`, and calls it reads:

```neura
let arena = arenas.of_confined();
let lib = result.expect<lookups.SymbolLookup>(lookups.library_lookup("./libmathops.so", arena), "load");
let function = optional.expect<segments.MemorySegment>(lib.find("add"), "find add");
let add = linkers.native_linker().downcall_handle(
    function, descriptors.of(layouts.Int32, layouts.Int32, layouts.Int32));
console.print_line(values.as_int32(add.invoke(5, 3)).to_string());  // 8
arena.close();
```

### Arenas and segments

An arena owns native memory. Allocation returns zeroed memory aligned as requested, up to 4,096 bytes. Closing a confined arena frees its memory and unloads the libraries loaded through it. The global arena is never closed; its memory and libraries are released when the run ends. Neura has one thread, so `of_shared()` is the same as `of_confined()`. There is no automatic arena: explicit closing follows the [resource rules](Stdlib.md#55-resources) of the standard library.

A segment made from a raw address, such as a symbol, a pointer that native code returned, or `of_address`, has size 0. As in Java, `reinterpret(size)` gives it an extent that native code defines. The library cannot check that extent. Values use the runner's native byte order, and `get_utf8` reads up to a NUL byte that must lie inside the segment.

### Layouts

A value layout has a carrier: the Neura type that holds the value. `ValueLayout` provides the fixed-width layouts. `Linker.canonical_layout` gives the platform's C types, `bool`, `char`, `short`, `int`, `long`, `long long`, `float`, `double`, `void*`, `size_t`, and `wchar_t`, with the sizes the runner reports. A plain `char` is signed or unsigned as the platform defines.

`struct_layout` places each member at the next offset aligned for it and pads the size to the largest alignment, as C does. Explicit `padding_layout` members add space; a struct passed by value must still have C's natural layout, because the platform ABI decides how its members travel. Unions and arrays cannot be passed by value; pass their address. Member names select offsets with `byte_offset("origin", "y")`.

### Calls

`invoke` takes untyped arguments (`...`) and converts each one to its layout:

| Layout | Accepted argument |
| --- | --- |
| Integer carriers | Any integer whose value fits |
| `Float32`, `Float64` | A `float32` or `float64`; a `float64` is rounded to `float32` |
| `Bool` | A `bool` |
| `Address` | A `MemorySegment`, `nil` for the null pointer, or a `string`, passed as a NUL-terminated copy that lives for the call |
| A struct | A `MemorySegment` holding exactly the struct's bytes |

The result is a `ForeignValue`. A pointer result is an `Address` segment of size 0. A struct result is copied into the global arena, or into the arena given to `invoke_with`. For a variadic C function such as `printf`, `downcall_handle(symbol, descriptor, first_variadic)` names the first variadic argument. The descriptor lists the layouts of the call's actual arguments, and those from `first_variadic` on must already be promoted: `int` or a wider integer, `double`, or an address.

## C imports

`import:c("header.h", "library.a", ...)` imports the declarations of a C header as a module. Any further strings name static libraries (`.a`, `.lib`) or object files (`.o`, `.obj`) to link into the image. A header-only import such as `import:c("stdlib.h")` calls functions that the C library already provides.

```neura
let mathops = import:c("mathops.h", "libmathops.a");
let clib = import:c("stdlib.h");

let sum = mathops.add(5, 3);                        // int add(int, int)
let point = mathops.make_point(3, 4);               // a struct returned by value
console.print_line(point.x.to_string());
console.print_line(mathops.MATHOPS_NAME);           // #define MATHOPS_NAME "mathops"
console.print_line(clib.abs(-9).to_string());
```

The module is named after the header in PascalCase, so `math-ops.h` becomes `MathOps`. The compiler finds the header beside the importing file, in its `-I` roots, in `--c-include` directories, and in the system include directories. Libraries are found beside the importing file. Shared libraries are rejected: load them with `SymbolLookup.library_lookup`.

### How it works

The compiler reads the header with libclang, which it loads at run time through the `third_party/neura-libclang` submodule. It generates the source of an ordinary Neura module whose functions call the header's functions through `Std.DynamicForeign`. That source then goes through the parser, lowering, optimization, and conversion like any other module. `neura c-bindings header.h` prints it.

At build time, the tool links the libraries named by every C import with `--whole-archive`, plus wrappers for the headers' `static inline` functions, into one shared object. It uses the system C compiler (`--cc`, else `$CC`, else `cc`). The libraries must be compiled with `-fPIC`; a link failure reports the linker's message and that hint. The shared object goes into the image's [`NATV` section](../runtime/NVR.md#imports-and-optional-source-maps) with the target it was linked for and the symbols the image uses. `neura run` loads it on the first call when the target matches, and unless native code is denied with `--deny-native` or `--deny-all`. A `.nir` file carries the linked object, so building from it needs neither the libraries nor a C compiler. Building twice with the same toolchain reproduces the image.

Only declarations written in the imported header itself are imported, not those of the headers it includes. Each one maps to Neura as follows:

| C | Neura |
| --- | --- |
| `_Bool`, `char`, `short`, `int`, `long`, `long long` and their unsigned forms, `float`, `double` | `bool` and the integer and float types of the same size; a plain `char` follows the platform's signedness |
| Any pointer, including function pointers | `MemorySegment` of size 0; a `const char*` parameter also gets an overload that takes a `string` |
| A struct | A class with one field per member, `layout()`, `of(...)`, and `read(segment)` as static functions, and `write_to(segment)`; passed and returned by value |
| An array member of a struct | A list field of its length |
| An enumerator | An `export const` of the enum's integer type with the C value |
| An object-like macro that is an integer, floating, or string constant | An `export const` |
| A global variable | `name_segment()`, `get_name()`, and `set_name(value)` unless it is `const` |
| A variadic function | A function whose further arguments are untyped: an integer passes as an `int` when it fits and a `long long` otherwise, a float as a `double`, a bool as an `int`, and a string or segment as an address |
| A `static inline` function | A call to an exported wrapper compiled into the image's native library |

C names are kept; a name that is a Neura keyword or built-in type name gets a trailing underscore. A declaration with no mapping is left out of the module, and using it is a compile error that names the reason. This covers function-like macros, bit-fields, unions passed by value, `long double`, `_Complex`, 128-bit integers, packed or explicitly aligned structs, and flexible array members. The generated module lists every such declaration in a comment at its top.

## Upcalls

`Linker.upcall_stub(target, descriptor, arena)` returns a native function pointer that calls a Neura function, the counterpart of Java's `upcallStub`. Native code such as `qsort` can call it with a comparator:

```neura
let compare = func(arguments: values.ForeignValue[list]) -> values.ForeignValue {
    let left = values.as_segment(arguments[0]).reinterpret(4).get_int32(0);
    let right = values.as_segment(arguments[1]).reinterpret(4).get_int32(0);
    return values.ForeignValue.Int32((left < right) ? -1 : (left > right) ? 1 : 0);
};
let stub = linker.upcall_stub(compare, descriptors.of(layouts.Int32, layouts.Address, layouts.Address), arena);
qsort.invoke(numbers, 5, 4, stub);
```

The target is a function value of type `func(ForeignValue[list]) -> ForeignValue`. It receives the arguments tagged by their layouts and returns `ForeignValue.Void` for a void descriptor. A struct argument is a copy that lives until the target returns. A result that does not match the descriptor's result layout traps.

Native code may call a stub only while a downcall from the same program is in progress and while the stub's arena is open. A call at any other time, such as from another thread or after the arena closes, ends the process with status 134. A trap in the target ends the run: native code receives zeros from that call and from every later call, and when the downcall returns the run reports the trap. Native frames are not unwound, as in Java. A variadic descriptor cannot be an upcall. The runner implements stubs with libffi closures and the image's `neura.ffi.upcall` callback ([NVM](../runtime/NVM.md#callbacks)), so record and replay cover upcalls: a replay calls the target again with the recorded arguments.

## Runtime services and tooling

C imports need libclang 14 or newer and its builtin headers at compile time (`libclang1-19` and `libclang-common-19-dev` on Debian and Ubuntu), found as [Tooling](Tooling.md#c-imports) describes, and a C compiler to link libraries. Running an image needs neither.


The library binds the `neura.ffi.*` host services ([NVM](../runtime/NVM.md#host-services)). Arguments and results travel as packed bytes in native byte order, described by a signature that names each C type. The runner calls native functions through libffi and loads libraries with the platform's loader.

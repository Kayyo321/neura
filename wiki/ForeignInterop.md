# Foreign interop

Neura programs can call native C code through `Std.DynamicForeign`, a standard library modeled on Java's Foreign Function and Memory API. Native code runs in the [Neura Virtual Machine](https://github.com/Kayyo321/neura/wiki/NVM)'s process as a host service. It never becomes part of the circuit. Every native operation crosses the runtime boundary as a host request that the runner grants with `--allow-native`.

## Safety and determinism

Native code is outside Neura's guarantees. A native function can read or write any memory in the runner's process, crash the process, or behave differently from run to run. `--allow-native` is the counterpart of Java's `--enable-native-access`. Grant it only to programs whose native libraries you trust.

Within those limits, the library checks what it can before native code runs:

- **Memory access.** Every read and write through a `MemorySegment` checks that its arena is still open and that the bytes lie inside the segment. A violation traps with `invalid state: arena closed` or `invalid argument: out of bounds`, before any native memory is touched.
- **Calls.** A call converts each argument to the layout its `FunctionDescriptor` gives it. An argument that does not fit, a wrong argument count, or a variadic argument that C's default argument promotions would widen traps with `invalid argument: ...`.
- **Denied capability.** Without `--allow-native`, the first native operation traps with `invalid state: native access denied: run with --allow-native` (status 134, `ExitCode.Abort`). Creating an arena or a layout needs no capability.
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

An arena owns native memory. Allocation returns zeroed memory aligned as requested, up to 4,096 bytes. Closing a confined arena frees its memory and unloads the libraries loaded through it. The global arena is never closed; its memory and libraries are released when the run ends. Neura has one thread, so `of_shared()` is the same as `of_confined()`. There is no automatic arena: explicit closing follows the [resource rules](https://github.com/Kayyo321/neura/wiki/Stdlib#55-resources) of the standard library.

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

Native code may call a stub only while a downcall from the same program is in progress and while the stub's arena is open. A call at any other time, such as from another thread or after the arena closes, ends the process with status 134. A trap in the target ends the run: native code receives zeros from that call and from every later call, and when the downcall returns the run reports the trap. Native frames are not unwound, as in Java. A variadic descriptor cannot be an upcall. The runner implements stubs with libffi closures and the image's `neura.ffi.upcall` callback ([NVM](https://github.com/Kayyo321/neura/wiki/NVM#callbacks)), so record and replay cover upcalls: a replay calls the target again with the recorded arguments.

## Runtime services

The library binds the `neura.ffi.*` host services ([NVM](https://github.com/Kayyo321/neura/wiki/NVM#host-services)). Arguments and results travel as packed bytes in native byte order, described by a signature that names each C type. The runner calls native functions through libffi and loads libraries with the platform's loader.

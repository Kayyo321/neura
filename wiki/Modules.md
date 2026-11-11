<!-- Generated from docs/lang/Modules.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Modules

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

A module groups related values, functions, and types under a name. Members marked `export` form its public interface; other members are internal to the module. Access an exported member with `Module.member`.

<!-- example: check -->
```neura
let Logger = module (
    export let default_level: string = "INFO";
    export let log = func(message: string) -> void;
);
```

A module can also export types and functions with implementations. A function signature without a body, such as `log` above, expresses an interface requirement. A module inheriting that interface supplies the implementation.

## Imports

Use `import:ModuleName` to bring a module into scope, normally under a short local alias:

<!-- example: check -->
```neura
let console = import:Std.Io.Console;
console.print_line("Hello");
```

An import alias shares the file's scope with the members the file declares, so choose an alias that no member of the file uses.

Use `let.promote` in a file or module to make an imported module's exports
available by their unqualified names. A named import also keeps its alias.
Use `_` to omit the alias.

<!-- example: check -->
```neura
let.promote clock = import:Std.Time.Clock;
let.promote _ = import:Std.Time.Duration;

let zero: Duration = Zero;
let current_clock: Clock = nil;
```

`let.promote _ = import:Std.Time.*;` promotes exports from the immediate
modules in the `Std.Time` directory. Wildcard imports do not recurse into
subdirectories, and cannot import internal standard library modules from
project source. Declarations and local bindings take precedence over
promoted names. Referring to a name provided by multiple promoted modules in
the same scope is an error. A promoted export remains qualified in its
defining module and is not automatically re-exported.

`let _ = import:ModuleName;` records the initialization dependency without
bringing any names into scope. For an ordinary value, `let _ = expression;`
evaluates the expression once and discards the result. This works for local,
file, and module bindings. Each occurrence has no name to read or redeclare.

For project-local modules, the qualified-name and file layout convention is described in [Project structure](ProjectStructure). The [multi-file example](https://github.com/Kayyo321/neura/blob/main/examples/modules/multi-file/main.nra) shows a project module importing another project module.

### C headers

`import:c("header.h", "library.a", ...)` imports a C header and links any static libraries or object files named after it. The compiler generates the module from the header's declarations; [Foreign interop](ForeignInterop#c-imports) defines the mapping and the native library the image carries.

<!-- example: fragment: imports mathops.h and libmathops.a, which the page does not provide -->
```neura
let mathops = import:c("mathops.h", "libmathops.a");
let clib = import:c("stdlib.h");
```

### Prebuilt NeuraIR modules

Use a quoted path to import a module from a prebuilt `.nir` artifact. Select a top-level project module after the closing quote when the artifact bundles several.

<!-- example: fragment: imports lib/stats.nir, a prebuilt module the page does not provide -->
```neura
let stats = import:"lib/stats.nir";
let helpers = import:"lib/stats.nir".Helpers;
```

Without a selector, the compiler chooses the module whose source filename convention matches the artifact's filename. If none matches, it uses the sole top-level project module when there is exactly one. Relative paths are searched beside the importing file and in its `-I` roots. More than one candidate is an error. For a named project import such as `import:Stats`, the compiler looks for `stats.nra` first and falls back to `stats.nir` if there is no source candidate. Wildcard imports enumerate source modules.

To package a library, write it as an ordinary module and run `neura emit-ir stats.nra -o lib/stats.nir`. The artifact includes its dependencies, compiled code, types, and initializers. It exposes the module's exported members with the same access rules as a source import. During compilation, compatible copies of a shared dependency join into one definition with one type identity and one initialization. Project dependencies share identity when the recorded source paths match. Incompatible copies or a project source included directly and through an artifact produce an error.

The compiler checks bundled standard library definitions against the installed library. Import `Std.*` by its usual source name. Build an importable library without a program entry override when the consuming application defines its own entry.

The executable and any newly emitted `.nir` contain the linked program. They can be built or run later without reopening the imported artifacts. [NeuraIR format](NirFormat)

### Standard library imports

The compiler reserves the `Std` namespace. Installed tools resolve `Std.*` below their installation's `share/neura/stdlib` directory. A checkout build can also resolve them from the checkout's `stdlib` directory. Project files and `-I` roots cannot supply replacements for these imports. Imports with an `Internal` component are accessible only from standard library files.

A module in that source tree may bind a bodyless, non-generic function to a versioned host service.

<!-- example: fragment: a host binding, which the compiler accepts only inside a module of the trusted standard library -->
```neura
let WriteResult = enum (Ok, Error(uint32));
let write_line = func(message: string) -> WriteResult host("neura.console.write_line", 1);
```

The result enum has exactly two variants. `Ok` represents success without a value and `Ok(T)` represents success with one value. `Error(uint32)` carries the host error code. Supported host data include Boolean and numeric values, strings, arrays, lists, and tuples made of supported types. The runner must register the named service with the matching version and signature. The compiler emits imports only for bindings reached by the built program. [NVM](NVM#host-calls-and-standard-output) defines value transfer and continuation behavior.

A module in that tree may also mark a function with a block body as a host callback, which a host service may call while it serves a request:

<!-- example: fragment: a host callback with an ellipsis for its body, accepted only inside a module of the trusted standard library -->
```neura
let dispatch = func(stub: int64, arguments: uint8[list]) -> uint8[list] {
    ...
} callback("neura.ffi.upcall", 1);
```

A callback is a non-generic module function whose parameters are host data and whose result is host data or void. The image includes a callback when the built program reaches a member of the module that declares it, and records it in the `CALB` table ([NVR](NVR#imports-and-optional-source-maps)). `Std.DynamicForeign` uses one for [upcalls](ForeignInterop#upcalls).

## Interfaces and inheritance

A module can inherit another module with `module:Base`. Its interface includes the inherited exports as well as its own. The derived module can implement inherited function signatures and use inherited implementations.

<!-- example: check -->
```neura
let Logger = module (
    export let default_level: string = "INFO";
    export let log = func(message: string) -> void;
);

let ConsoleLogger = module:Logger (
    export let.override log = func(message: string) -> void {
        // Write the message to the console.
    }
);
```

Use `let.override` or `const.override` when defining an inherited member, including when supplying a body for an interface signature. For example, an application entry point uses:

<!-- example: check -->
```neura
let program = import:Std.Program;

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        return program.ExitCode.Ok;
    }
);
```

Use `const.override` when the replacement binding should remain constant. For example, an implementation can fix an inherited configuration value so it cannot be reassigned accidentally:

<!-- example: check -->
```neura
let Defaults = module (
    export let timeout_ms: int32 = 1000;
);

let FixedDefaults = module:Defaults (
    export const.override timeout_ms: int32 = 5000;
);
```

The constant applies to the binding; it does not by itself establish deep immutability or prohibit further inheritance. A module declared with `const`, as described below, cannot itself be inherited from.

See `examples/modules/module-interface.nra` for an interface with exported data, a class, a required function, and an inherited implementation.

### Program entry

A program implements the standard library requirement `Std.Program.Entry.main(string[]) -> ExitCode`. The compiler maps the library's `ExitCode.Ok` to process status 0, `ExitCode.Failure` to 1, and `ExitCode.Status(int)` to the supplied status. An explicit status outside 0 through 255 traps with `invalid argument: exit status`. The other variants name conventional statuses; most follow the shell convention of 128 plus the Linux number of the signal that would end a native process:

| Variant | Status | Meaning |
| --- | --- | --- |
| `SeriousFailure` | 2 | A serious failure, such as misuse of the program |
| `CommandNotFound` | 127 | A requested command was not found |
| `IllegalInstruction` | 132 | SIGILL: an illegal instruction or damaged program |
| `Breakpoint` | 133 | SIGTRAP: a breakpoint or trace trap |
| `Abort` | 134 | SIGABRT: a failed assertion or forced abort |
| `ArithmeticError` | 136 | SIGFPE: an arithmetic error, including integer division by zero and overflow |
| `OutOfMemory` | 137 | SIGKILL, as sent by an out-of-memory killer |
| `BusError` | 138 | SIGBUS: a misaligned or invalid memory access |
| `SegmentationFault` | 139 | SIGSEGV: an access to memory the program does not own |
| `CpuTimeExceeded` | 152 | SIGXCPU: the CPU time limit was exceeded |
| `FileSizeExceeded` | 153 | SIGXFSZ: the file size limit was exceeded |

SIGXCPU and SIGXFSZ are signals 24 and 25 on Linux for x86, Arm, and RISC-V, and 30 and 31 on MIPS, where a shell reports 158 and 159; Neura always uses 152 and 153. Integer division by zero raises SIGFPE (136), not SIGTRAP (133). `Std.Program.status_of(code)` returns the status a variant stands for, and `Std.Program.Process.exit` accepts either a status or an `ExitCode`. A runtime trap ends the run with the status of the variant that names its class ([Errors](Errors)). A program with more than one entry override is rejected.

The former built-in `SysIo` and `Test` modules are retired. Importing either is an error that names the replacement: `Std.Io.Console` and `Std.Program` for `SysIo`, and `Std.Test` for `Test`. Images built while they existed keep running under `neura run` as long as they import only `neura.console.write_line`; images that called the retired assertion services `neura.test.assert` or `neura.test.assert_equal_int32` no longer bind, because the runner no longer provides those services.

## Constant modules

A module declared with `const` cannot be inherited from:

<!-- example: check -->
```neura
const Xyz = module (
    export let value: int32 = 1;
);

// Invalid: Xyz is a const module.
// let Derived = module:Xyz ();
```

A `let` module can be used as a base; a `const` module closes that inheritance point. This rule concerns inheritance and does not by itself specify the mutability of every member.

## Initialization

A program's files, modules, and top-level statements initialize before the entry function runs, in this order:

1. Every module or file global whose initializer is written as a literal (a number, a string, `true`, `false`, `nil`, or a fieldless enum variant) holds its value when the image loads. A number may have a leading `-`.
2. Each file runs after every file it imports. Files that do not depend on each other run in the order the compiler received them. Files that import each other, directly or through other files, form a cycle: the cycle starts once every file it imports from outside the cycle has run, and its files then run in the order the compiler received them.
3. Within a file, top-level statements, the remaining initializers of file
   globals and module members, and construction of singleton instances run
   in source order. This includes singletons declared inside classes. An
   active use during startup can also initialize a class before `main`.

Then `main` runs. The same initialization runs before each test under `neura test`.
Ordinary static field initializers follow [the declaring type's initialization
rules](Types#static-initialization).

<!-- example: fragment: imports Settings, which needs a second source file, settings.nra, that the page describes in prose -->
```neura
let console = import:Std.Io.Console;
let settings = import:Settings;   // settings.nra initializes first

console.print_line("starting");               // runs second
let banner: string = "[" + settings.label + "]";  // runs third
let Counter = class ( export let value: int32 = 0; );
let shared = new Counter();                   // runs fourth
```

Whether an initializer is a literal is decided by how it is written, not by its value: `let size: int32 = 2 * 3;` is computed at program start in source order, while `let size: int32 = 6;` is set when the image loads. Reading a global before its initializer has run, for example from a file-level statement that precedes the declaration of a module whose member it reads, is a fault that ends the run with status 139 (`SegmentationFault`, [Errors](Errors)). Within one module or file, a global cannot be read before its declaration; the frontend rejects that.

A startup trap ends the run before `main`. If a static initializer traps
during an active use, execution ends before that use completes. Cyclic imports
between files can run in the order above when every read finds an initialized
global.

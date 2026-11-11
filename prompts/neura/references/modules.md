# Modules, imports, and program structure

A module groups values, functions, classes, enums, interfaces, and nested modules under a name. Members marked `export` form its public interface; the rest are private to the module. Specification: [Modules](https://github.com/Kayyo321/neura/blob/main/docs/lang/Modules.md) and [Project structure](https://github.com/Kayyo321/neura/blob/main/docs/lang/ProjectStructure.md).

## Contents

- [Declaring a module](#declaring-a-module)
- [Imports](#imports)
- [Simplifying imports with `let.promote`](#simplifying-imports-with-letpromote)
- [Module interfaces and inheritance](#module-interfaces-and-inheritance)
- [Constant modules](#constant-modules)
- [Multi-file projects](#multi-file-projects)
- [The program entry point](#the-program-entry-point)
- [Initialization order](#initialization-order)
- [Prebuilt NeuraIR modules](#prebuilt-neurair-modules)
- [The `Std` namespace](#the-std-namespace)

## Declaring a module

<!-- example: check -->
```neura
let Geometry = module (
    export const Pi: float64 = 3.141592653589793;
    export let unit_name: string = "m";

    export let Point = class (
        export let x: float64 = 0.0;
        export let y: float64 = 0.0;
    );

    export let Quadrant = enum (First, Second, Third, Fourth);

    export let Units = module (                  // nested module
        export let to_feet = func(meters: float64) -> float64 => meters * 3.28084;
    );

    let square = func(value: float64) -> float64 => value * value;   // private

    export let area = func(radius: float64) -> float64 {
        return Pi * square(radius);
    };
);

let ring = Geometry.area(2.0);
let feet = Geometry.Units.to_feet(ring);
let origin = new Geometry.Point();
let where = Geometry.Quadrant.First;
```

- Access exports as `Module.member`; nested types and modules chain: `new Geometry.Point()`, `Geometry.Quadrant.First`.
- Every member ends with `;`, including one whose block body ends at `}` (`};`).
- A module is not a type. `func(store: Store)` fails with `'Store' is not a type`. To pass behavior around at run time, use an [interface](syntax.md#interfaces) or a class.
- Imports may appear inside a module body; they are visible only there.

## Imports

<!-- example: check -->
```neura
let console = import:Std.Io.Console;          // standard library module
let maps = import:Std.Collections.HashMap;    // module named after its type
let program = import:Std.Program;

let greet = func {
    console.print_line("Hello");
    let ages = maps.create<string, int>();    // factory function of the module
    let typed: maps.HashMap<string, int> = ages;   // the class inside the module
};
```

- `import:Qualified.Name` evaluates to the module. Bind it to a short `snake_case` alias.
- Each file imports what it uses; an alias in one file is not visible in another. There is no prelude: even `Std.Core.Optional` and `Std.Core.Result` must be imported (primitive companion member calls such as `"x".trim()` are the exception).
- The alias shares the file's scope with the file's own declarations. A clash is `duplicate declaration of 'name'`; rename the alias (for example `hashing` for `Std.Core.Hash` when the file exports `hash`, or `int32s` for `Std.Core.Int32`).
- `let _ = import:Some.Module;` records an initialization dependency without introducing a name.
- An unknown module: `cannot resolve import 'Acme.Nothing'`. Check the spelling and the `-I` source roots.

## Simplifying imports with `let.promote`

`let.promote` makes a module's exports usable without a qualifier. It works at file level and inside module bodies, not inside functions.

<!-- example: check -->
```neura
let.promote clock = import:Std.Time.Clock;       // promote and keep the alias
let.promote _ = import:Std.Time.Duration;        // promote without an alias
let.promote _ = import:Std.Core.Optional;
let.promote _ = import:Std.Core.Result;

let zero: Duration = Zero;                        // was duration.Zero
let wait = of_seconds(3);                         // was duration.of_seconds(3)
let current_clock: Clock = nil;                   // was clock.Clock
let maybe: Optional<int> = Optional<int>.None;    // was optional.Optional<int>.None
let present = is_some<int>(maybe);                // was optional.is_some<int>(...)
```

Both `Std.Core.Optional` and `Std.Core.Result` export `value_or`, `expect`, `map`, and other helpers, so with both promoted those names are ambiguous:

<!-- example: rejected: ambiguous promoted name 'value_or' -->
```neura
let.promote optional = import:Std.Core.Optional;
let.promote _ = import:Std.Core.Result;

let maybe: Optional<int> = Optional<int>.Some(2);
let fallback = value_or<int>(maybe, 0);
```

Qualify through the alias instead: `optional.value_or<int>(maybe, 0)`.

Wildcards promote every module directly inside a namespace directory:

<!-- example: check -->
```neura
let.promote _ = import:Std.Time.*;               // Duration, Instant, Clock, LocalDate, ...
let.promote maps = import:Std.Collections.HashMap;

let span: Duration = of_minutes(5);
let table: HashMap<string, int> = maps.create<string, int>();
```

Rules and advice:

- Promotion only applies to imports: `let.promote _ = SomeLocalModule;` fails with `let.promote requires an unannotated import alias`.
- Wildcards do not recurse into subdirectories and cannot reach `Internal` standard library modules.
- Your own declarations and local bindings win over promoted names. Using a name that two promoted modules both export is an error, so be careful promoting many modules that share function names: most collection modules export `create`, and several also export `of`. The pattern that scales is **promote for type names, keep an alias for factories**: `let.promote maps = import:Std.Collections.HashMap;` then `HashMap<K, V>` as the type and `maps.create<K, V>()` to build one.
- After `let.promote _ = import:Std.Collections.*;`, the name `HashMap` resolves to the class, so `HashMap.create<...>()` fails with `unknown or inaccessible member 'create'`. Import the module under an alias for its functions.
- Promoted names are not re-exported from your module.

## Module interfaces and inheritance

A bodyless function signature in a module is a *requirement*. Another module inherits it with `module:Base` and implements it with `let.override`. The derived module's interface is the base's exports plus its own, and inherited implementations call the derived module's overrides.

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;

let Logger = module (
    export let default_level: string = "INFO";

    export let LogEntry = class (
        export let level: string = "INFO";
        export let message: string = "";

        export let format = func -> string {
            return "[" + level + "] " + message;
        };
    );

    export let log = func(message: string) -> void;          // requirement: no body

    export let log_entry = func(entry: LogEntry) -> void {   // uses the requirement
        log(entry.format());
    };
);

let ConsoleLogger = module:Logger (                           // implements Logger
    export let prefix: string = "stdout";

    export let.override log = func(message: string) -> void {
        console.print_line(prefix + " -> " + message);
    };
);

let QuietLogger = module:Logger (
    export const.override default_level: string = "WARN";     // override data, constant
    export let.override log = func(message: string) -> void {
    };
);

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        ConsoleLogger.log("started");
        let entry = new Logger.LogEntry();
        entry.message = "inherited " + QuietLogger.default_level;
        ConsoleLogger.log_entry(entry);
        QuietLogger.log("never shown");
        return program.ExitCode.Ok;
    };
);
```

```text
stdout -> started
stdout -> [INFO] inherited WARN
```

- `let.override` (or `const.override` to make the replacement constant) is required for every inherited member you redefine, including when supplying a body for a requirement.
- A module inherits one base. The entry point is the most common use: `module:program.Entry` implements `Std.Program.Entry.main`.
- Module interfaces are resolved at compile time; modules are not values you can pass. For run-time polymorphism use `interface` and classes ([syntax.md](syntax.md#interfaces)).
- Do not call a base module's requirement directly (`Logger.log(...)`); call it through an implementing module.

## Constant modules

<!-- example: rejected: cannot inherit a constant module -->
```neura
const Settings = module (
    export let value: int32 = 1;
);

let Derived = module:Settings (
);
```

Declaring a module with `const` closes it to inheritance. It does not make its members constant.

## Multi-file projects

Give the compiler one or more source roots with `-I`. It also searches beside the importing file. A qualified name maps to a path: each namespace component becomes a lowercase kebab-case directory and the last component a kebab-case `.nra` file that declares a module with that last name.

```text
my-app/
├── src/                          # source root
│   ├── main.nra                  # entry module
│   └── acme/
│       ├── text/
│       │   └── greeter.nra       # import:Acme.Text.Greeter  (declares `Greeter`)
│       ├── http-client.nra       # import:Acme.HttpClient    (declares `HttpClient`)
│       └── http-client/
│           └── retry.nra         # import:Acme.HttpClient.Retry
└── tests/
    └── greeter-tests.nra
```

<!-- example: fragment: src/acme/text/greeter.nra of the project above; a second file imports it -->
```neura
let Greeter = module (
    export let greet = func(name: string) -> string => "Hello, " + name + "!";
);
```

<!-- example: fragment: src/main.nra of the project above, which needs greeter.nra beside it -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;
let greeter = import:Acme.Text.Greeter;

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        console.print_line(greeter.greet("Neura"));
        return program.ExitCode.Ok;
    };
);
```

<!-- example: fragment: tests/greeter-tests.nra of the project above, run with -I src -->
```neura
let test = import:Std.Test;
let greeter = import:Acme.Text.Greeter;

let GreeterTests = module (
    export let test_greet = func {
        test.assert_equal_string("Hello, A!", greeter.greet("A"));
    };
);
```

```sh
neura build src/main.nra -o app.nvr        # main.nra finds acme/ beside it
neura run app.nvr                          # Hello, Neura!
neura test -I src tests/greeter-tests.nra  # tests need the source root
```

- The imported file's module is what the import returns; only its exports are visible.
- A module file and a directory can share a name, so a module can have child modules. Importing a parent does not import its children.
- More than one matching file is an ambiguity error. There is no package manifest or dependency manager.

## The program entry point

A program has exactly one module that overrides `Std.Program.Entry.main(args: string[]) -> ExitCode`. Without one, `neura build` fails with `the program has no entry point: no module overrides Std.Program.Entry.main`; a second override is rejected.

`args` holds the arguments after `--` on `neura run image.nvr -- a b`, or every argument of a native executable. The returned `ExitCode` sets the process status:

| `ExitCode` | Status |
| --- | --- |
| `Ok` | 0 |
| `Failure` | 1 |
| `Status(n)` | `n`, 0 through 255 (anything else traps) |
| `SeriousFailure` | 2 |
| `CommandNotFound` | 127 |
| `IllegalInstruction` | 132 |
| `Breakpoint` | 133 |
| `Abort` | 134 |
| `ArithmeticError` | 136 |
| `OutOfMemory` | 137 |
| `BusError` | 138 |
| `SegmentationFault` | 139 |
| `CpuTimeExceeded` | 152 |
| `FileSizeExceeded` | 153 |

`program.status_of(code)` gives the number. `program.Process.exit(status)` (an `int` or an `ExitCode`) ends the run immediately from anywhere. `program.Environment.get(name)` returns `Lookup.Found(value)`, `Lookup.Missing`, or `Lookup.Failed(error)`. `Entry`, `Environment`, and `Process` are nested modules of `Std.Program`, so they are reached through the one import.

## Initialization order

Before `main` (and before each test under `neura test`):

1. Globals initialized with a literal (`6`, `"text"`, `true`, `nil`, a fieldless enum variant, optionally negated numbers) hold their value when the image loads. `2 * 3` is not a literal.
2. Each file runs after the files it imports; independent files run in command-line order; files in an import cycle run once everything outside the cycle they import has run.
3. Within a file, top-level statements, the remaining global and module member initializers, and singleton construction run in source order.

Top-level statements are allowed in any file and run at startup, but a program still needs an entry module. Reading a global before its initializer has run ends the run with status 139; within one file, the compiler rejects reading a global before its declaration. Class statics initialize lazily on first active use ([syntax.md](syntax.md#static-members-and-singletons)).

## Prebuilt NeuraIR modules

A library can be compiled once to a `.nir` artifact and imported by path:

```sh
neura emit-ir stats.nra -o lib/stats.nir
```

<!-- example: fragment: imports lib/stats.nir, a prebuilt artifact this page does not provide -->
```neura
let stats = import:"lib/stats.nir";              // the module that matches the file name
let helpers = import:"lib/stats.nir".Helpers;    // pick one module of several
```

Relative paths are searched beside the importing file and in `-I` roots. For a named import such as `import:Stats`, the compiler tries `stats.nra`, then `stats.nir`. The artifact carries its dependencies; a library meant to be imported must not define its own entry override when the consumer does.

## The `Std` namespace

`Std.*` always resolves to the trusted standard library installed with the tool (below `share/neura/stdlib`, or a checkout's `stdlib/`). Project files and `-I` roots cannot replace it, and modules with an `Internal` component are reachable only from the library itself. The retired built-in modules `SysIo` and `Test` are errors that name their replacements (`Std.Io.Console` with `Std.Program`, and `Std.Test`). C headers are imported with `import:c("header.h")`; see [foreign.md](foreign.md).

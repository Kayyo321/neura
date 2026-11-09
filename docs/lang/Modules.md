# Modules

A module groups related values, functions, and types under a name. Members marked `export` form its public interface; other members are internal to the module. Access an exported member with `Module.member`.

```neura
let Logger = module (
    export let default_level: string = "INFO";
    export let log = func(string message) -> void;
);
```

A module can also export types and functions with implementations. A function signature without a body, such as `log` above, expresses an interface requirement. A module inheriting that interface supplies the implementation.

## Imports

Use `import:ModuleName` to bring a module into scope, normally under a short local alias:

```neura
let io = import:SysIo;
io.println("Hello");
```

For project-local modules, the proposed qualified-name and file layout convention is described in [Project structure](ProjectStructure.md). The [multi-file example](../../examples/modules/multi-file/main.nra) shows a project module importing another project module.

## Interfaces and inheritance

A module can inherit another module with `module:Base`. Its interface includes the inherited exports as well as its own. The derived module can implement inherited function signatures and use inherited implementations.

```neura
let ConsoleLogger = module:Logger (
    export let.override log = func(string message) -> void {
        // Write the message to the console.
    }
);
```

Use `let.override` or `const.override` when defining an inherited member, including when supplying a body for an interface signature. For example, an application entry point uses:

```neura
let App = module:io.Entry (
    export let.override main = func(string[] args) -> Entry.ExitCode {
        return Entry.ExitCode.Ok;
    }
);
```

Use `const.override` when the replacement binding should remain constant. For example, an implementation can fix an inherited configuration value so it cannot be reassigned accidentally:

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

## Constant modules

A module declared with `const` cannot be inherited from:

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

1. Every module or file global whose initializer is written as a literal (a number, a string, `true` or `false`, or an enum variant, optionally with a leading `-`) holds its value when the image loads.
2. Each file runs after every file it imports. Files that do not depend on each other, and files that import each other, run in the order the compiler received them.
3. Within a file, top-level statements and the remaining initializers of the file's globals and of its modules' members run in source order, by their position in the file.

Then `main` runs. The same initialization runs before each test under `neura test`.

```neura
let io = import:SysIo;
let settings = import:Settings;   // settings.nra initializes first

io.println("starting");                       // runs second
let banner: string = "[" + settings.label + "]";  // runs third
let Counter = class ( export let value: int32 = 0; );
let shared = new Counter();                   // runs fourth
```

Whether an initializer is a literal is decided by how it is written, not by its value: `let size: int32 = 2 * 3;` is computed at program start in source order, while `let size: int32 = 6;` is set when the image loads. Reading a global before its initializer has run, for example from a file-level statement that precedes the declaration of a module whose member it reads, is a fault that ends the run with status 70. Within one module or file, a global cannot be read before its declaration; the frontend rejects that.

Initialization has no separate failure handling: a trap during initialization ends the run before `main`. Cyclic imports between files are allowed only as far as the order above permits; reads of globals that are not yet initialized fault.


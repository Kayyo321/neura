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

For project-local modules, the proposed qualified-name and file layout convention is described in [Project structure](ProjectStructure.md). The [multi-file example](../../examples/modules/multi-file/main.nra) shows a project module importing another project module. This convention is not yet backed by a compiler in this repository.

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

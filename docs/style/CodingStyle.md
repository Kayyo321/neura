# Neura Coding Style Guide

This document defines the coding style and naming conventions for Neura (`.nra`) code: examples, fixtures, the standard library, and programs. The standard library follows it, so where the guide is silent, write what the library does. Following one style keeps the code consistent, readable, and maintainable across the repository.

---

## 1. Naming Conventions

### Summary Table

| Category | Convention | Examples |
| :--- | :--- | :--- |
| **Modules** | `PascalCase` | `Console`, `HelloWorld`, `ConsoleLogger` |
| **Classes and interfaces** | `PascalCase` | `LogEntry`, `HttpRequest`, `UserSession` |
| **Enums & Variants** | `PascalCase` | `ExitCode.Ok`, `HttpMethod.Get`, `Status.NotFound` |
| **Constants (`const`)** | `PascalCase` | `const Thousand = 1000;`, `const MaxRetries = 5;` |
| **Functions & Methods** | `snake_case` | `main`, `log_entry`, `format_message`, `calculate_sum` |
| **Variables, fields, and `last` bindings** | `snake_case` | `default_level`, `show_timestamp`, `entry_count` |
| **Function Parameters** | `snake_case` | `message`, `user_id`, `max_buffer_size` |
| **Import Aliases** | `snake_case` / short | `console`, `program`, `net_client` |
| **Tags** | `PascalCase`, as registered | `#Deprecated`, `#NoDiscard`, `#Since("0.2.0")` |
| **Source Files** | `kebab-case.nra` | `hello-world.nra`, `module-interface.nra` |

---

### Detailed Rules

#### Modules (`PascalCase`)
Modules must be defined using `PascalCase`:
<!-- example: check -->
```neura
let Logger = module (
    // ...
);

let ConsoleLogger = module:Logger (
    // ...
);
```

#### Classes (`PascalCase`)
Class definitions must use `PascalCase`:
<!-- example: check -->
```neura
export let LogEntry = class (
    export last level: string;
    export last message: string;

    export let init = func(entry_level: string, text: string) {
        level = entry_level;
        message = text;
    };
);

export let NetworkBuffer = class (
    // ...
);
```

#### Enums and Enum Variants (`PascalCase`)
Both enum type names and their variants must use `PascalCase`:
<!-- example: check -->
```neura
let program = import:Std.Program;

let HttpMethod = enum (Get, Post, Delete);

let status = func -> program.ExitCode => program.ExitCode.Ok;
```

#### Constants (`PascalCase`)
Constants defined with `const` must use `PascalCase`:
<!-- example: check -->
```neura
const Thousand = 1000;
const MaxRetries = 3;
const DefaultTimeoutMs = 5000;
```

#### Functions and Methods (`snake_case`)
All function definitions, exported routines, and class methods must use `snake_case`:
<!-- example: check -->
```neura
let program = import:Std.Program;

let Logging = module (
    export let log = func(message: string) -> void;

    export let log_twice = func(message: string) {
        log(message);
        log(message);
    };
);

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        return program.ExitCode.Ok;
    };
);
```

#### Variables, Fields, and `last` Bindings (`snake_case`)
Local variables, exported module variables, class fields, and `last` bindings use `snake_case`. A `last` binding is a value assigned once, not a named constant, so it is not `PascalCase`:
<!-- example: check -->
```neura
// Module-level variables
export let default_level: string = "INFO";
export let prefix: string = "stdout";

// Class fields
export let Config = class (
    export let show_timestamp: bool = false;
    export let retry_limit: int = 3;
);

// Local variables
let describe = func(config: Config) -> string {
    last retries = config.retry_limit.to_string();
    let text = prefix + ": " + retries;
    if (config.show_timestamp) {
        text += " with timestamps";
    }
    return text;
};
```

#### Import Aliases (`snake_case`)
An import alias shares its file's scope with the members the file declares. Choose an alias that no member of the file uses, such as `hashing` for `Std.Core.Hash` in a module that exports its own `hash` function:
<!-- example: check -->
```neura
let hashing = import:Std.Core.Hash;
let optional = import:Std.Core.Optional;
```

#### Function Parameters (`snake_case`)
All function parameters must use `snake_case`. A parameter that would have the name of a field it initializes takes another name, so that the constructor can assign the field by its bare name:
<!-- example: check -->
```neura
let format_log = func(log_message: string, log_level: int) -> string {
    return "[" + log_level.to_string() + "] " + log_message;
};
```

---

## 2. File and Directory Structure

- **Source Files**: Name source files in `kebab-case` with the `.nra` extension (e.g., `hello-world.nra`, `module-interface.nra`).
- **Directories**: Use lowercase or `kebab-case` for directory names (e.g., `examples/hello-world/`, `docs/style/`).
- **One Primary Module per File**: In general, files should represent a single primary module or a set of closely related components.
- **Imports first**: Put the file's imports at its top, one per line, before any declaration.

---

## 3. Formatting and Layout

### Indentation and Spacing
- Use **4 spaces** for indentation. Do not use tabs.
- Place spaces around binary operators (`+`, `-`, `=`, `->`, etc.):
  <!-- example: fragment: formatting illustration: one statement that uses level and message from an enclosing function -->
  ```neura
  let full_message = "[" + level + "] " + message;
  ```
- Leave one space after colons in type annotations:
  <!-- example: fragment: formatting illustration: two field lines of a declaration, not a complete declaration -->
  ```neura
  export let level: string;
  export let prefix: string = "stdout";
  ```
- Leave a blank line between members of a module or class that span more than one line.

### Delimiters and Semicolons
- Use parentheses for module, class, and interface bodies, with the opening parenthesis on the same line and the closing parenthesis on a new line. Function bodies use braces in K&R / 1TBS style.
- **Every declaration ends with `;`**, whatever its value. A declaration whose value is a function, class, interface, or module body ends with `};` or `);`, and an overload group ends with `;` after its last alternative. The compiler rejects a missing one (`E0201`), and `neura check --fix` inserts it.
- **Statement blocks take no `;`**: the bodies of `if`, `else`, `while`, `for`, `do`, `match` statements, `scope`, `select`, and a bare `{ }` block end at `}`.
  <!-- example: check -->
  ```neura
  let console = import:Std.Io.Console;

  let Report = module (
      export let print_all = func(lines: string[]) {
          for (let line in lines) {
              if (line.byte_length > 0) {
                  console.print_line(line);
              }
          }
      };
  );
  ```

---

## 4. Bindings: `let`, `last`, and `const`

Choose the introducer by how often the binding is assigned:

| Introducer | Use it for | Name |
| :--- | :--- | :--- |
| `last` | A local or a field that is assigned once and never again, including one whose value a branch chooses (`last x: T;` assigned on every path) | `snake_case` |
| `let` | A local or a field that is reassigned, and every function, class, module, and enum declaration | `snake_case`, or `PascalCase` for types and modules |
| `const` | A named constant at module, file, or static level | `PascalCase` |

- **Prefer `last` to `let`** for every local and field that is not reassigned. A reader then knows the value does not change, and the compiler checks it. The standard library writes almost every local this way.
- **Prefer a deferred `last` to a placeholder.** When a branch chooses the value, declare `last x: T;` and assign it in each branch instead of initializing a `let` with a value that is overwritten.
- **Keep `const` for named constants**, not for locals. A destructuring declaration is the exception: `last` does not destructure, so write `const [a, b] = pair;` for constant names from a tuple.
- A `last` or `const` binding to an object still lets the object change; it fixes only the binding.

<!-- example: check -->
```neura
const DefaultTimeoutMs = 1000;

let timeout_ms = func(retrying: bool, attempts: int) -> int {
    last base: int;
    if (retrying) {
        base = 5000;
    } else {
        base = DefaultTimeoutMs;
    }
    last extra = attempts * 100;
    return base + extra;
};
```

---

## 5. Classes

### Fields
- Declare a field that the constructor sets once as `last`. An exported `last` field is read-only outside the class, so it needs no getter: `export last year: int;`.
- Give an exported field that the class changes, and that callers only read, a get-only accessor: `export let size: int;` followed by `| get: size;`.
- Keep a field unexported, with no accessor, when callers do not need it.
- Prefer a field to a zero-argument method that only returns stored state, such as `export let code = func -> string => code_value;`. Keep a method when the value is computed, takes arguments, does work a reader would not expect from a field read, or satisfies an interface member, which an accessor cannot.

### Accessor Clauses
- Write each accessor clause on its own line after the field, indented one level (4 spaces) under it. Put a short body on the clause line; open a block body on the clause line and close it with `};` at the clause's indentation:
  <!-- example: check -->
  ```neura
  let Gauge = class (
      export let level: int = 0;
          | get: level;
          | set: {
              if (value < 0) {
                  trap_with_message("level must not be negative");
              }
              level = value;
          };
  );
  ```
- Write `get` before `set` when a field has both.
- Leave a blank line after a field's last clause when another member follows.
- Add a setter only when the class checks or reacts to a write. A setter that only stores, `| set: field = value;`, is worth writing only together with a getter that does more, or to keep the field write-only outside the class. A plain exported `let` field already reads and writes without accessors.

### Construction
- When the type is already named by an annotation, a result type, or a parameter, construct it with `new` alone: `= new;`, `= new(arguments);`, or `return new(arguments);`. Name the class after `new` only where nothing else names it:
  <!-- example: check -->
  ```neura
  let Point = class (
      export last x: int;
      export last y: int;

      export let init = func() {
          x = 0;
          y = 0;
      } or func(left: int, top: int) {
          x = left;
          y = top;
      };

      export let moved = func(dx: int, dy: int) -> Point => new(x + dx, y + dy);
  );

  let origin: Point = new;
  let corner = new Point(3, 4);
  let [start, end]: [Point, Point] = new, new(10, 10);
  ```
- Name the parameters of `init` differently from the fields they set (`left`, not `x`), so the body assigns each field by its bare name.

### Anonymous Classes
- Use an anonymous class for a small group of fields that one or two places pass around, and a named class for anything with methods or used across modules. An anonymous class with accessors or `const` or `last` fields suits a single value, such as one settings object; it is a class of its own, so no other place can name its type. Lay out a multi-line anonymous class like a class definition, with the closing parenthesis on its own line:
  <!-- example: check -->
  ```neura
  let settings: class (
      export let retries: int = 3;
      export let verbose: bool = false;
  ) = new;
  ```

---

## 6. Tags

- Write [tags](../lang/Tags.md) on their own lines directly above the declaration, after its documentation comment, at the declaration's indentation.
- Put several short tags in one `#[...]` group, and give a tag with long arguments a line of its own:
  <!-- example: check -->
  ```neura
  let Shapes = module (
      /// The area of a square of the given width.
      #[Since("0.2.0"), Stable]
      #Deprecated(as_of: "0.3.0", use_instead: area, reason: "it takes the width twice")
      export let square_area = func(width: int32, height: int32) -> int32 {
          return width * height;
      };

      export let area = func(width: int32) -> int32 {
          return width * width;
      };
  );
  ```
- Tag an enum variant or a parameter on the same line, before its name: `#Likely Ready,` and `func(#Unit("ms") timeout: int64)`.
- On a field with accessor clauses, the tags go above the field and the clauses stay indented under it; never put a tag between the field and its clauses:
  <!-- example: check -->
  ```neura
  let Meter = class (
      #Unit("mm")
      export let reading: int = 0;
          | get: reading;
          | set: {
              if (value >= 0) {
                  reading = value;
              }
          };

      #Deprecated(use_instead: reading)
      export let raw: int = 0;
  );
  ```
- **Tags on public API.** The standard library tags its exported declarations this way, and a library should do the same:
  - `#Since("<version>")` on each module, on each exported type, and on each member added after the module's first release.
  - `#Final` on a class that is not designed to be extended: value classes, resource handles, and most classes of a library.
  - `#NoDiscard` on a function whose result must be used: a `Result` or `Optional`, a new value that the call computes, or a resource handle that must be closed. On a class, it covers every call that returns the class; give a `reason` when it is not obvious.
  - `#Deprecated(as_of:, use_instead:, reason:)` before removing a declaration, with `use_instead` naming the replacement so that `neura check --fix` can apply it.
  - `#Internal` on a declaration that the other modules of the library use and its users must not.
- Avoid tags on the members of an anonymous class that should stay fields-only: a tag makes the body a class of its own, so it no longer matches the same fields written elsewhere. Declare a named class when its fields need tags.
- Write `#Pure`, `#Immutable`, `#ThreadSafe`, and `#Synchronized` where the property is part of a declaration's contract, such as an exported function of a library or a class that tasks share; the compiler checks each one, so the tag documents a promise that holds. Do not tag a function only to help the optimizer, which infers purity on its own.
- Mark a class `#Synchronized` when every exported method should take the guard, rather than tagging each method, and keep a monitor's methods short: a method that waits holds its guard. Keep a collection that tasks share in a `Std.Concurrent.Guarded` field or with an owner task:
  <!-- example: check -->
  ```neura
  /// A count that tasks share.
  #Synchronized
  let Hits = class (
      let count: int64 = 0;

      export let record = func() -> int64 {
          count = count + 1;
          return count;
      };

      export let total = func() -> int64 => count;
  );

  /// A value that tasks share without a guard.
  #Immutable
  let Point = class (
      export last x: int32;
      export last y: int32;

      export let init = func(left: int32, top: int32) {
          x = left;
          y = top;
      };
  );
  ```
- Discard a `#NoDiscard` result on purpose with `let _ = call();`, not by silencing the warning.

---

## 7. Comments
- Use `//` for single-line comments and `///` for [documentation comments](../lang/CoreLanguage.md#documentation-comments) on declarations.
- Place comments above the relevant code or inline with a single space after code:
  <!-- example: check -->
  ```neura
  let program = import:Std.Program;

  let HelloWorld = module:program.Entry ( // Inherits from Std.Program.Entry
      // Implementation of entrypoint
      export let.override main = func(args: string[]) -> program.ExitCode {
          return program.ExitCode.Ok;
      };
  );
  ```

---

## 8. Comprehensive Example

Here is a complete example demonstrating the coding style conventions:

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;

// Constants: PascalCase
const DefaultPort = "8080";
const MaxConnectionAttempts = 5;

// Modules: PascalCase
let ServiceMonitor = module (
    // Variables: snake_case
    export let default_service_name: string = "auth_service";

    // Enums: PascalCase, variants PascalCase
    export let Health = enum (Healthy, Degraded, Down);

    // Classes: PascalCase, documented, then tagged
    /// The settings of one monitored service.
    #Final
    export let ServiceConfig = class (
        // Set once by the constructor: last
        export last service_name: string;
        export let retry_count: int = MaxConnectionAttempts;

        // Changed by the class, read by callers: a get-only accessor
        export let checks_run: int = 0;
            | get: checks_run;

        export let init = func(name: string) {
            service_name = name;
        };

        // Methods: snake_case
        #NoDiscard
        export let connection_info = func() -> string {
            return service_name + ":" + DefaultPort;
        };

        export let record_check = func() {
            checks_run++;
        };
    );

    // Functions: snake_case
    export let run_health_check = func(config: ServiceConfig) -> Health {
        config.record_check();
        console.print_line("Running check for: " + config.connection_info());
        return Health.Healthy;
    };
);

// Module inheritance: PascalCase
let App = module:program.Entry (
    // Main entrypoint: snake_case
    export let.override main = func(args: string[]) -> program.ExitCode {
        let monitor_config: ServiceMonitor.ServiceConfig = new(ServiceMonitor.default_service_name);
        last health = ServiceMonitor.run_health_check(monitor_config);
        if (health == ServiceMonitor.Health.Healthy) {
            console.print_line("Checks run: " + monitor_config.checks_run.to_string());
        }

        return program.ExitCode.Ok;
    };
);
```

Running the program prints:

```text
Running check for: auth_service:8080
Checks run: 1
```

---

## 9. Do's and Don'ts

| Do | Don't | Reason |
| :--- | :--- | :--- |
| `const Thousand = 1000;` | `const THOUSAND = 1000;` or `const thousand = 1000;` | Constants use `PascalCase`. |
| `let LogEntry = class ( ... );` | `let log_entry = class ( ... );` | Classes use `PascalCase`. |
| `let ConsoleLogger = module ( ... );` | `let console_logger = module ( ... );` | Modules use `PascalCase`. |
| `export let log_message = func(...)` | `export let logMessage = func(...)` | Functions use `snake_case`. |
| `let default_level: string = "INFO";` | `let DefaultLevel: string = "INFO";` | Variables use `snake_case`. |
| `func(target_host: string)` | `func(targetHost: string)` | Parameters use `snake_case`. |
| `program.ExitCode.Ok` | `program.exit_code.ok` | Enums and variants use `PascalCase`. |
| `let f = func { ... };` and `let C = class ( ... );` | `let f = func { ... }` | Every declaration ends with `;`. |
| `if (ready) { ... }` | `if (ready) { ... };` | Statement blocks take no `;`. |
| `last total = a + b;` | `let total = a + b;` never reassigned | A value assigned once is `last`. |
| `last timeout: int;` assigned once in each branch | `let timeout: int = 0;` overwritten in each branch | A value chosen once is a deferred `last`. |
| `let origin: Point = new;` | `let origin: Point = new Point();` | The annotation already names the class. |
| `export last year: int;` | `export let year = func -> int => year_value;` | A stored value is a field, read as `date.year`. |
| `export let size: int;` with `\| get: size;` | `export let size = func -> int => count;` | A value the class changes and callers read is a get-only field. |
| `#NoDiscard` above a function that returns a `Result` | A `Result` that callers may drop silently | Dropping the result warns. |
| `let _ = file.write(bytes);` | `-Wno-unused-result` | Discard a result on purpose where you mean to. |

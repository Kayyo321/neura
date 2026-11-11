# Neura Coding Style Guide

This document defines the official coding style and naming conventions for Neura (`.nra`) codebases. Adhering to these guidelines ensures consistency, readability, and maintainability across the repository.

---

## 1. Naming Conventions

### Summary Table

| Category | Convention | Examples |
| :--- | :--- | :--- |
| **Modules** | `PascalCase` | `Console`, `HelloWorld`, `ConsoleLogger` |
| **Classes** | `PascalCase` | `LogEntry`, `HttpRequest`, `UserSession` |
| **Enums & Variants** | `PascalCase` | `ExitCode.Ok`, `HttpMethod.Get`, `Status.NotFound` |
| **Constants** | `PascalCase` | `const Thousand = 1000;`, `const MaxRetries = 5;` |
| **Functions & Methods** | `snake_case` | `main`, `log_entry`, `format_message`, `calculate_sum` |
| **Variables (Local & Member)** | `snake_case` | `default_level`, `show_timestamp`, `entry_count` |
| **Function Parameters** | `snake_case` | `message`, `user_id`, `max_buffer_size` |
| **Import Aliases** | `snake_case` / short | `console`, `program`, `net_client` |
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
    export let level: string;
    export let message: string;
);

export let NetworkBuffer = class (
    // ...
);
```

#### Enums and Enum Variants (`PascalCase`)
Both enum type names and their variants must use `PascalCase`:
<!-- example: fragment: naming illustration: a return statement shown outside a function -->
```neura
// Enum type and variant access
return program.ExitCode.Ok;
```

#### Constants (`PascalCase`)
Constants defined with `const` must use `PascalCase`:
<!-- example: check -->
```neura
const Thousand = 1000;
const MaxRetries = 3;
const DefaultTimeoutMs = 5000;
```

#### Assign-once bindings (`last`, `snake_case`)
A `last` binding is a local or a field that is assigned once, not a named constant, so it uses `snake_case`. Prefer `last` over `let` for a local or field that never changes after its first assignment, and over a `let` with a placeholder value when the value is chosen by a branch. Keep `const` with `PascalCase` for module, file, and static constants.
<!-- example: check -->
```neura
let Connection = class (
    export last host: string;
    export last port: int = 8080;

    export let init = func(host: string) {
        instance.host = host;
    };
);

let timeout_ms = func(retrying: bool) -> int {
    last timeout: int;
    if (retrying) {
        timeout = 5000;
    } else {
        timeout = 1000;
    }
    return timeout;
};
```

#### Functions and Methods (`snake_case`)
All function definitions, exported routines, and class methods must use `snake_case`:
<!-- example: fragment: naming illustration: module members shown without their module, with LogEntry and program undefined -->
```neura
export let log = func(message: string) -> void;

export let log_entry = func(entry: LogEntry) -> void {
    log(entry.format());
};

export let.override main = func(args: string[]) -> program.ExitCode {
    return program.ExitCode.Ok;
};
```

#### Variables and Properties (`snake_case`)
Local variables, exported module variables, and class fields must use `snake_case`:
<!-- example: fragment: naming illustration: module members and local bindings together, with Logger undefined -->
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
let user_entry = new Logger.LogEntry();
let is_active = true;
```

#### Import Aliases (`snake_case`)
An import alias shares its file's scope with the members the file declares. Choose an alias that no member of the file uses, such as `hashing` for `Std.Core.Hash` in a module that exports its own `hash` function:
<!-- example: check -->
```neura
let hashing = import:Std.Core.Hash;
let optional = import:Std.Core.Optional;
```

#### Function Parameters (`snake_case`)
All function parameters must use `snake_case`:
<!-- example: fragment: naming illustration: a module member shown without its module and with a placeholder body -->
```neura
export let format_log = func(log_message: string, log_level: int) -> string {
    // ...
};
```

---

## 2. File and Directory Structure

- **Source Files**: Name source files in `kebab-case` with the `.nra` extension (e.g., `hello-world.nra`, `module-interface.nra`).
- **Directories**: Use lowercase or `kebab-case` for directory names (e.g., `examples/hello-world/`, `docs/style/`).
- **One Primary Module per File**: In general, files should represent a single primary module or a set of closely related components.

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

### Delimiters and Semicolons
- Use parentheses for module and class definitions, with the opening parenthesis on the same line and the closing parenthesis on a new line. Function bodies use braces in K&R / 1TBS style.
- Module and class definitions end with `);`:
  <!-- example: fragment: formatting illustration: a module declaration with a placeholder body and no program import -->
  ```neura
  let HelloWorld = module:program.Entry (
      // ...
  );
  ```
- Terminate statements and every declaration with a semicolon `;`. A declaration whose value is a function, class, or module body ends with `};` or `);`. Statement blocks such as `if`, `while`, and `for` take no `;`:
  <!-- example: fragment: formatting illustration: a bodyless signature and a statement shown as module members -->
  ```neura
  export let log = func(message: string) -> void;
  console.print_line(prefix + " -> " + message);
  ```

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
- Write `get` before `set` when a field has both, unless the setter is the point of the field.
- Leave a blank line after a field's last clause when another member follows.
- Prefer a get-only field (`| get: field;`) to a method that only returns a field, such as `export let code = func -> string => code_value;`. Keep a method when the value is computed, takes arguments, or does work a reader would not expect from a field read.
- Add a setter only when the class checks or reacts to a write. A setter that only stores, `| set: field = value;`, is worth writing only together with a getter that does more, or to keep the field write-only outside the class.
### Construction and anonymous classes
- When an annotation already names the class, construct it with `new` alone, `= new;` or `= new(arguments);`, rather than repeating the class. Name the class after `new` where nothing else does:
  <!-- example: fragment: style illustration: Point is a class declared elsewhere -->
  ```neura
  let origin: Point = new;
  let corner = new Point(3, 4);
  ```
- Use an anonymous class for a small group of fields that one or two places pass around, and a named class for anything with methods or used across modules. An anonymous class with accessors or `const` or `last` fields suits a single value, such as one settings object; it is a class of its own, so no other place can name its type. Lay out a multi-line anonymous class like a class definition, with the closing parenthesis on its own line:
  <!-- example: check -->
  ```neura
  let settings: class (
      export let retries: int = 3;
      export let verbose: bool = false;
  ) = new;
  ```

### Comments
- Use `//` for single-line comments.
- Place comments above the relevant code or inline with a single space after code:
  <!-- example: fragment: formatting illustration: a module declaration with a placeholder body and no program import -->
  ```neura
  let HelloWorld = module:program.Entry ( // Inherits from Std.Program.Entry
      // Implementation of entrypoint
      export let.override main = func(args: string[]) -> program.ExitCode {
          // ...
      };
  );
  ```

---

## 4. Comprehensive Example

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

    // Classes: PascalCase
    export let ServiceConfig = class (
        export let retry_count: int = MaxConnectionAttempts;
        export let enable_ssl: bool = true;

        // Methods: snake_case
        export let get_connection_info = func() -> string {
            return default_service_name + ":" + DefaultPort;
        };
    );

    // Functions: snake_case
    export let ping_service = func(target_host: string, timeout_seconds: int) -> void;

    export let run_health_check = func(config: ServiceConfig) -> void {
        console.print_line("Running check for: " + config.get_connection_info());
    };
);

// Module inheritance: PascalCase
let App = module:program.Entry (
    // Main entrypoint: snake_case
    export let.override main = func(args: string[]) -> program.ExitCode {
        let monitor_config = new ServiceMonitor.ServiceConfig();
        ServiceMonitor.run_health_check(monitor_config);

        return program.ExitCode.Ok;
    };
);
```

Running the program prints:

```text
Running check for: auth_service:8080
```

---

## 5. Do's and Don'ts

| Do | Don't | Reason |
| :--- | :--- | :--- |
| `const Thousand = 1000;` | `const THOUSAND = 1000;` or `const thousand = 1000;` | Constants use `PascalCase`. |
| `let LogEntry = class ( ... );` | `let log_entry = class ( ... );` | Classes use `PascalCase`. |
| `let ConsoleLogger = module ( ... );` | `let console_logger = module ( ... );` | Modules use `PascalCase`. |
| `export let log_message = func(...)` | `export let logMessage = func(...)` | Functions use `snake_case`. |
| `let default_level: string = "INFO";` | `let DefaultLevel: string = "INFO";` | Variables use `snake_case`. |
| `last timeout: int;` assigned once in each branch | `let timeout: int = 0;` overwritten in each branch | A value chosen once is a `last` binding. |
| `func(target_host: string)` | `func(targetHost: string)` | Parameters use `snake_case`. |
| `program.ExitCode.Ok` | `program.exit_code.ok` | Enums and variants use `PascalCase`. |
| `let origin: Point = new;` | `let origin: Point = new Point();` | The annotation already names the class. |


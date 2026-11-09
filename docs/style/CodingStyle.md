# Neura Coding Style Guide

This document defines the official coding style and naming conventions for Neura (`.nra`) codebases. Adhering to these guidelines ensures consistency, readability, and maintainability across the repository.

---

## 1. Naming Conventions

### Summary Table

| Category | Convention | Examples |
| :--- | :--- | :--- |
| **Modules** | `PascalCase` | `SysIo`, `HelloWorld`, `ConsoleLogger` |
| **Classes** | `PascalCase` | `LogEntry`, `HttpRequest`, `UserSession` |
| **Enums & Variants** | `PascalCase` | `ExitCode.Ok`, `HttpMethod.Get`, `Status.NotFound` |
| **Constants** | `PascalCase` | `const Thousand = 1000;`, `const MaxRetries = 5;` |
| **Functions & Methods** | `snake_case` | `main`, `log_entry`, `format_message`, `calculate_sum` |
| **Variables (Local & Member)** | `snake_case` | `default_level`, `show_timestamp`, `entry_count` |
| **Function Parameters** | `snake_case` | `message`, `user_id`, `max_buffer_size` |
| **Import Aliases** | `snake_case` / short | `io`, `sys`, `net_client` |
| **Source Files** | `kebab-case.nra` | `hello-world.nra`, `module-interface.nra` |

---

### Detailed Rules

#### Modules (`PascalCase`)
Modules must be defined using `PascalCase`:
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
```neura
// Enum type and variant access
return Entry.ExitCode.Ok;
```

#### Constants (`PascalCase`)
Constants defined with `const` must use `PascalCase`:
```neura
const Thousand = 1000;
const MaxRetries = 3;
const DefaultTimeoutMs = 5000;
```

#### Functions and Methods (`snake_case`)
All function definitions, exported routines, and class methods must use `snake_case`:
```neura
export let log = func(string message) -> void;

export let log_entry = func(LogEntry entry) -> void {
    log(entry.format_message());
}

export let.override main = func(string[] args) -> Entry.ExitCode {
    return Entry.ExitCode.Ok;
}
```

#### Variables and Properties (`snake_case`)
Local variables, exported module variables, and class fields must use `snake_case`:
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

#### Function Parameters (`snake_case`)
All function parameters must use `snake_case`:
```neura
export let format_log = func(string log_message, int log_level) -> string {
    // ...
}
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
  ```neura
  let full_message = "[" + level + "] " + message;
  ```
- Leave one space after colons in type annotations:
  ```neura
  export let level: string;
  export let prefix: string = "stdout";
  ```

### Delimiters and Semicolons
- Use parentheses for module and class definitions, with the opening parenthesis on the same line and the closing parenthesis on a new line. Function bodies use braces in K&R / 1TBS style.
- Module and class definitions end with `);`:
  ```neura
  let HelloWorld = module:io.Entry (
      // ...
  );
  ```
- Terminate statements, variable bindings, and function signatures with a semicolon `;`:
  ```neura
  export let log = func(string message) -> void;
  io.println(prefix + " -> " + message);
  ```

### Comments
- Use `//` for single-line comments.
- Place comments above the relevant code or inline with a single space after code:
  ```neura
  let HelloWorld = module:io.Entry ( // Inherits from SysIo.Entry
      // Implementation of entrypoint
      export let.override main = func(string[] args) -> Entry.ExitCode {
          // ...
      }
  );
  ```

---

## 4. Comprehensive Example

Here is a complete example demonstrating the coding style conventions:

```neura
let io = import:SysIo;

// Constants: PascalCase
const DefaultPort = 8080;
const MaxConnectionAttempts = 5;

// Modules: PascalCase
let ServiceMonitor = module (
    // Variables: snake_case
    export let default_service_name: string = "auth_service";

    // Enums: PascalCase
    // Classes: PascalCase
    export let ServiceConfig = class (
        export let retry_count: int = MaxConnectionAttempts;
        export let enable_ssl: bool = true;

        // Methods: snake_case
        export let get_connection_info = func() -> string {
            return default_service_name + ":" + DefaultPort;
        }
    );

    // Functions: snake_case
    export let ping_service = func(string target_host, int timeout_seconds) -> void;

    export let run_health_check = func(ServiceConfig config) -> void {
        io.println("Running check for: " + config.get_connection_info());
    }
);

// Module inheritance: PascalCase
let App = module:io.Entry (
    // Main entrypoint: snake_case
    export let.override main = func(string[] args) -> Entry.ExitCode {
        let monitor_config = new ServiceMonitor.ServiceConfig();
        ServiceMonitor.run_health_check(monitor_config);

        return Entry.ExitCode.Ok;
    }
);
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
| `func(string target_host)` | `func(string targetHost)` | Parameters use `snake_case`. |
| `Entry.ExitCode.Ok` | `Entry.exit_code.ok` | Enums and variants use `PascalCase`. |


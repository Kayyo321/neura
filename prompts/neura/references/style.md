# Neura coding style

The conventions of [Coding style](https://github.com/Kayyo321/neura/blob/main/docs/style/CodingStyle.md), which the standard library follows. Follow them in every `.nra` file.

## Naming

| Kind | Convention | Examples |
| --- | --- | --- |
| Modules | `PascalCase` | `Console`, `ConsoleLogger` |
| Classes, interfaces, singletons | `PascalCase` | `LogEntry`, `HttpRequest` |
| Enums and variants | `PascalCase` | `ExitCode.Ok`, `Status.NotFound` |
| Constants (`const`) | `PascalCase` | `const MaxRetries = 5;` |
| Functions, methods | `snake_case` | `format_message`, `main` |
| Variables, fields, `last` bindings, parameters | `snake_case` | `default_level`, `user_id`, `last total = ...;` |
| Tags | as registered, `PascalCase` | `#NoDiscard`, `#Since("0.2.0")` |
| Import aliases | short `snake_case` | `console`, `program`, `maps`, `int32s` |
| Files | `kebab-case.nra` | `hello-world.nra`, `http-client.nra` |
| Directories | lowercase `kebab-case` | `acme/http-client/` |
| Tests | `test_` + `snake_case`, in a `PascalCase` module ending in `Tests` | `MathTests.test_sum_to` |

## Layout

- Four spaces, no tabs. Spaces around binary operators and `->`; one space after `:` in annotations.
- Module and class bodies: `(` on the declaration line, members indented, `);` on its own line. Function bodies: K&R braces.
- **Every declaration ends with `;`** (`};` after a block body, `);` after a class or module). **Statement blocks take none** (`if`, `while`, `for`, `match`, `scope`, `select`, bare `{ }`).
- One primary module per file, named after the file (`greeter.nra` declares `Greeter`).
- Group imports at the top of the file, one per line; aliases that do not clash with the file's own names.
- `//` comments above the code they describe, or inline after one space. `///` documents a declaration and goes **above its tags**.
- A blank line between members that span several lines, and after a field's last accessor clause.

## Modern idioms

| Write | Not | Why |
| --- | --- | --- |
| `last total = a + b;` | `let total = a + b;` that is never reassigned | A value assigned once is `last`; the compiler checks it. |
| `last label: string;` assigned in each branch | `let label = "";` overwritten in each branch | A deferred `last` replaces a placeholder value. |
| `const MaxRetries = 3;` at module or file level | `const` for a local | `const` is for named constants; locals use `last`. |
| `let origin: Point = new;`, `return new(x, y);` | `let origin: Point = new Point();` | The annotation or result type already names the class. |
| `export last year: int;` | `export let year = func -> int => year_value;` | A stored value is a read-only field, read `date.year`. |
| `export let size: int = 0;` + `\| get: size;` | `export let size = func -> int => count;` | A value the class changes and callers read is a get-only field. |
| `export let level: int = 0;` + `\| get: level;` + `\| set: { check; level = value; };` | `get_level()` and `set_level(v)` methods | Accessors keep `obj.level = v` and run the check. |
| `#Final` on a class not designed for extension | an open class by default | Value classes and resource handles are `#Final` in the library. |
| `#NoDiscard` on a function returning `Result`/`Optional` or a resource | a result callers may drop silently | Dropping it then warns `W0003`. |
| `let _ = file.write(bytes);` | `file.write(bytes);` | Drop a `#NoDiscard` result on purpose. |
| `#Synchronized` class shared by tasks | a mutable class passed between tasks | Only monitors and `#Immutable` objects can be shared. |

## Example

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;

const DefaultPort = "8080";
const MaxConnectionAttempts = 5;

let ServiceMonitor = module (
    export let default_service_name: string = "auth_service";

    export let Health = enum (Healthy, Degraded, Down);

    /// The settings of one monitored service.
    #Final
    export let ServiceConfig = class (
        export last service_name: string;
        export let retry_count: int = MaxConnectionAttempts;

        export let checks_run: int = 0;
            | get: checks_run;

        export let init = func(name: string) {
            service_name = name;
        };

        #NoDiscard
        export let connection_info = func() -> string {
            return service_name + ":" + DefaultPort;
        };

        export let record_check = func() {
            checks_run++;
        };
    );

    export let run_health_check = func(config: ServiceConfig) -> Health {
        config.record_check();
        console.print_line("Running check for: " + config.connection_info());
        return Health.Healthy;
    };
);

let App = module:program.Entry (
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

```text
Running check for: auth_service:8080
Checks run: 1
```

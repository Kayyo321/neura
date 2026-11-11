# Neura coding style

The official conventions from [Coding style](https://github.com/Kayyo321/neura/blob/main/docs/style/CodingStyle.md). Follow them in every `.nra` file.

## Naming

| Kind | Convention | Examples |
| --- | --- | --- |
| Modules | `PascalCase` | `Console`, `ConsoleLogger` |
| Classes, interfaces, singletons | `PascalCase` | `LogEntry`, `HttpRequest` |
| Enums and variants | `PascalCase` | `ExitCode.Ok`, `Status.NotFound` |
| Constants (`const`) | `PascalCase` | `const MaxRetries = 5;` |
| Functions, methods | `snake_case` | `format_message`, `main` |
| Variables, fields, parameters | `snake_case` | `default_level`, `user_id` |
| Import aliases | short `snake_case` | `console`, `program`, `maps`, `int32s` |
| Files | `kebab-case.nra` | `hello-world.nra`, `http-client.nra` |
| Directories | lowercase `kebab-case` | `acme/http-client/` |
| Tests | `test_` + `snake_case`, in a `PascalCase` module ending in `Tests` | `MathTests.test_sum_to` |

## Layout

- Four spaces, no tabs. Spaces around binary operators and `->`; one space after `:` in annotations.
- Module and class bodies: `(` on the declaration line, members indented, `);` on its own line. Function bodies: K&R braces.
- End every statement and every declaration with `;`, including a function, class, or module member whose body ends at `}` or `)`. Statement blocks (`if`, `while`, `for`, `match`, `scope`, bare `{ }`) take no `;`.
- One primary module per file, named after the file (`greeter.nra` declares `Greeter`).
- Group imports at the top of the file, one per line; aliases that do not clash with the file's own names.
- `//` comments above the code they describe, or inline after one space. `///` for documentation of declarations.

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;

const DefaultPort = "8080";
const MaxConnectionAttempts = 5;

let ServiceMonitor = module (
    export let default_service_name: string = "auth_service";

    export let Health = enum (Healthy, Degraded, Down);

    export let ServiceConfig = class (
        export let retry_count: int = MaxConnectionAttempts;
        export let enable_ssl: bool = true;

        export let get_connection_info = func() -> string {
            return default_service_name + ":" + DefaultPort;
        };
    );

    export let run_health_check = func(config: ServiceConfig) -> void {
        console.print_line("Running check for: " + config.get_connection_info());
    };
);

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let monitor_config = new ServiceMonitor.ServiceConfig();
        ServiceMonitor.run_health_check(monitor_config);
        return program.ExitCode.Ok;
    };
);
```

```text
Running check for: auth_service:8080
```

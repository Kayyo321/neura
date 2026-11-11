# The standard library in practice

The standard library is Neura source installed with the tool and compiled with each program; an image contains only what the program reaches. This page shows how to use it. For exact signatures, grep [stdlib-api.md](stdlib-api.md). The specification is [Standard library](https://github.com/Kayyo321/neura/blob/main/docs/lang/StandardLibrary.md) and [Stdlib](https://github.com/Kayyo321/neura/blob/main/docs/lang/Stdlib.md).

## Contents

- [How the library is organized](#how-the-library-is-organized)
- [Console output and formatting](#console-output-and-formatting)
- [Strings and text](#strings-and-text)
- [Optional, Result, and Error](#optional-result-and-error)
- [Collections](#collections)
- [Numbers and math](#numbers-and-math)
- [Files and paths](#files-and-paths)
- [JSON](#json)
- [Time](#time)
- [Randomness](#randomness)
- [Environment, exit, and logging](#environment-exit-and-logging)
- [Tests with Std.Test](#tests-with-stdtest)
- [Module map](#module-map)

## How the library is organized

- **One module per type.** `Std.Collections.ArrayList` is the file `stdlib/std/collections/array-list.nra`. The module `ArrayList` exports the class `ArrayList<T>` and functions that build one (`create`, `of`, `filled`, `wrap`).
- So with `let lists = import:Std.Collections.ArrayList;` the **type** is `lists.ArrayList<string>` and a **new value** is `lists.create<string>()`. Several classes have private constructors; always prefer the module's factory functions over `new`.
- Enums live in their module too: `optional.Optional<int>.Some(1)`, `result.Result<int>.Ok(1)`, `ordering.Ordering.Less`.
- Expected failures return `Result<T>` (an `Ok(T)` or `Err(Error)`); absent values are `Optional<T>` (`Some(T)` or `None`). Violated preconditions (a bad index, an invalid argument) trap.
- Functions that touch the outside world (files, network, clock, environment, entropy) are host services. They are granted by default and can be denied with `--deny-*` options, in which case they return an `Io` error of kind `CapabilityDenied`.

## Console output and formatting

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        console.print_line("plain text, no formatting");       // stdout + newline
        console.write("no newline; ");                         // stdout, as is
        console.println("{}, {}!", "hello", "Neura");          // formatted + newline
        console.println("{1} before {0}", "second", "first");  // positional
        console.println("[{:>6}] [{:<6}] [{:^6}]", "r", "l", "c");
        console.println("{:08.3f} {:+d} {:x} {:#b}", 3.14159, 42, 255, 5);
        console.println("{{literal braces}} {:?}", "quoted");
        let text = console.format("{} items", 3);              // format to a string
        console.print_line(text);
        console.eprintln("to stderr: {}", true);               // stderr
        return program.ExitCode.Ok;
    };
);
```

```text
plain text, no formatting
no newline; hello, Neura!
first before second
[     r] [l     ] [  c   ]
0003.142 +42 ff 0b101
{literal braces} "quoted"
3 items
```

- `print_line`, `write`, `error_line` take one `string` and do no formatting (safe for text containing braces).
- `print`, `println`, `eprint`, `eprintln`, and `format` take a pattern and any values: `{}` next argument, `{0}` by index, `{:spec}` with fill/align `<^>`, sign `+`, `#`, `0`, width, `.precision`, and type `d b o x X c f e g a s ?`. `{{` and `}}` are literal braces.
- Invalid patterns trap; the `try_` forms (`try_println`, `try_format`) return `Result` instead.
- `console.read_line()` returns `Result<Optional<string>>` (`None` at end of input).

## Strings and text

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;
let strings = import:Std.Text.Strings;
let builders = import:Std.Core.StringBuilder;

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let csv = " alpha,beta,gamma ";
        let parts = csv.trim().split(",");                 // string[list]
        console.print_line(strings.join(parts, " | "));
        console.print_line(parts[1].to_upper_case());
        console.print_line(csv.contains("beta").to_string());
        console.print_line("abc".starts_with("ab").to_string());
        console.print_line("a-b-c".replace("-", "+"));
        console.print_line("7".pad_start(3, "0"));
        console.print_line("naïve".byte_length.to_string() + " bytes, " +
                           "naïve".scalar_count().to_string() + " scalars");
        console.print_line("hello".slice(1, 3) + "hello".substring(3));
        console.print_line(strings.index_of("hello", "l").to_string());

        let builder = builders.create();
        for (let i in 0 .. 3) {
            builder.append("#").append_int(i as int64);
        }
        console.print_line(builder.to_string());
        return program.ExitCode.Ok;
    };
);
```

```text
alpha | beta | gamma
BETA
true
true
a+b+c
007
6 bytes, 5 scalars
ello
2
#0#1#2
```

- Member calls like `text.trim()` come from the `Std.Core.String` companion and need no import. Every `Std.Text.Strings` function whose first parameter is a `string` also works as a member (`text.index_of("l")`, `text.lines()`, `text.strip_prefix("a")`, which returns `Optional<string>`). `Std.Text.Strings.join(parts, separator)` takes a list first, so call it through the module.
- Offsets are UTF-8 byte offsets and must fall on character boundaries; a bad offset traps.
- `Std.Text.Regex` compiles regular expressions; `text.matches(pattern)`, `replace_all`, and `split_regex` use it.
- Parse numbers with the companions: `int32.parse("42")`, `int64.parse(text)`, `float64.parse(text)`; each returns `Result`.

## Optional, Result, and Error

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;
let error = import:Std.Core.Error;
let optional = import:Std.Core.Optional;
let result = import:Std.Core.Result;

let parse_port = func(text: string) -> result.Result<int> {
    match (int32.parse(text)) {
        result.Result.Ok(value) => {
            if (value < 1 || value > 65535) {
                return result.Result<int>.Err(error.custom("port.range", "port out of range"));
            }
            return result.Result<int>.Ok(value);
        },
        result.Result.Err(failure) => {
            return result.Result<int>.Err(error.context(failure, "reading the port"));   // propagate
        },
    }
};

let find_user = func(id: int) -> optional.Optional<string> {
    return id == 1 ? optional.Optional<string>.Some("ada") : optional.Optional<string>.None;
};

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        for (let text in ["8080", "99999", "http"]) {
            let outcome = parse_port(text);
            let shown = match (outcome) {
                result.Result.Ok(port) => "port " + port.to_string(),
                result.Result.Err(failure) => "error: " + error.message(failure),
            };
            console.print_line(shown);
        }
        console.print_line(optional.value_or<string>(find_user(2), "nobody"));
        let name = optional.expect<string>(find_user(1), "user 1 exists");   // traps if None
        let length = optional.map<string, int>(find_user(1), func(s: string) -> int => s.byte_length);
        console.print_line(name + " " + optional.is_some<int>(length).to_string());
        let port = result.value_or<int>(parse_port("x"), 80);
        console.print_line(port.to_string());
        return program.ExitCode.Ok;
    };
);
```

```text
port 8080
error: port out of range
error: reading the port: parse error at 0: invalid character
nobody
ada true
80
```

- There is no `?` operator: propagate an error with an explicit `match` arm that returns it, usually wrapped with `error.context(cause, "what was being attempted")` or `result.with_context<T>(r, "...")`.
- Construct with type arguments (`result.Result<int>.Ok(1)`); match without them (`result.Result.Ok(v)`).
- Helpers on `Optional`: `is_some`, `is_none`, `value_or`, `value_or_else`, `expect`, `map`, `and_then`, `filter`, `or_else`, `to_result`. On `Result`: `is_ok`, `is_err`, `value_or`, `expect`, `to_optional`, `with_context`, `map`, `and_then`, `map_error`. All are generic: pass type arguments.
- `Error` is an enum: `Io { kind, message, path }`, `Parse { kind, offset, message }`, `Encoding { offset, message }`, `Custom(ErrorDetail)`, `Context { message, cause }`. Build with `error.custom(code, message)` and `error.context(cause, message)`; read with `error.message(e)` and `error.root_cause(e)`. Match `error.Error.Io { kind, message: _, path: _ }` to test `kind == error.IoErrorKind.NotFound`.
- `Error` is not transferable to tasks; send `error.to_portable(e)` instead ([concurrency.md](concurrency.md)).

## Collections

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;
let array_lists = import:Std.Collections.ArrayList;
let maps = import:Std.Collections.HashMap;
let sets = import:Std.Collections.HashSet;
let tree_maps = import:Std.Collections.TreeMap;
let sorting = import:Std.Collections.Sorting;
let lists = import:Std.Collections.Lists;
let optional = import:Std.Core.Optional;
let ordering = import:Std.Core.Ordering;
let strings = import:Std.Text.Strings;

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        // Built-in list plus helpers
        let numbers: int[list] = [5, 3, 9, 1];
        sorting.sort<int>(numbers, int32.compare);                 // in place
        lists.insert_at<int>(numbers, 0, 0);
        console.println("{} {} {}", numbers[0], numbers[1], numbers.length);

        // ArrayList: a class with many methods
        let names = array_lists.create<string>();
        names.add("Lin");
        names.add("Ada");
        names.sort(strings.compare);                              // a function value as comparator
        console.print_line(names.to_string());

        // HashMap
        let ages = maps.create<string, int>();
        ages.set("ada", 36);
        ages.set("lin", 29);
        ages.merge("ada", 1, func(old: int, extra: int) -> int => old + extra);
        console.println("{} {} {}", ages.get_or("ada", -1), ages.contains_key("bob"), ages.size());
        match (ages.get("lin")) {
            optional.Optional.Some(age) => { console.println("lin is {}", age); },
            optional.Optional.None => { console.print_line("no lin"); },
        }

        // Ordered iteration: TreeMap with a comparator
        let by_length = func(a: string, b: string) -> ordering.Ordering => int32.compare(a.byte_length, b.byte_length);
        let sorted = tree_maps.create<string, int>(by_length);
        sorted.set("bb", 2);
        sorted.set("a", 1);
        for (let entry in sorted) {                                 // entries are [K, V]
            console.println("{}={}", entry[0], entry[1]);
        }

        // HashSet
        let seen = sets.create<int>();
        seen.add(3);
        seen.add(3);
        console.println("set size {}", seen.size());
        return program.ExitCode.Ok;
    };
);
```

```text
0 1 5
[Ada, Lin]
37 false 2
lin is 29
a=1
bb=2
set size 1
```

- Comparators are `func(T, T) -> Std.Core.Ordering.Ordering` (`Less`, `Equal`, `Greater`). Companion `compare` functions fit directly as function values: `int32.compare`, `int64.compare`, `Std.Text.Strings.compare`. `Std.Core.Comparators` has `reversed`, `by_key`, and `then`.
- Keys and set elements use `===`/`equals` and `hash_code`, so primitives, strings, and classes that override both work as keys. `maps.with_keys<K, V>(hash, same)` supplies custom functions.
- Iterating a map yields `[K, V]` tuples; read them with `entry[0]` and `entry[1]` (the loop variable cannot be destructured). `keys()`, `values()`, and `entries()` return lists.
- Modifying a collection while iterating it traps. Collect changes, then apply them, or use `remove_if`.
- Others: `ArrayDeque`, `LinkedList`, `PriorityQueue` (`create<T>(comparator)`), `TreeSet`, `BitSet`, `IntMap`, `StringMap`, `OrdinalMap`, `ImmutableList`/`Set`/`Map` (`of(...)`), and `Sequence` for lazy `map`/`filter`/`fold` pipelines (`array_list.sequence()`).
- Collections are not transferable between tasks.

## Numbers and math

<!-- example: check -->
```neura
let math = import:Std.Math.Math;
let int32s = import:Std.Core.Int32;
let bigs = import:Std.Math.BigInteger;

let hypotenuse = math.sqrt(3.0 * 3.0 + 4.0 * 4.0);   // float64 functions (an fdlibm port)
let angle = math.atan2(1.0, 1.0);
let clamped = int32s.clamp(120, 0, 100);
let largest = int32s.Max;
let maybe = int32s.checked_mul(largest, 2);          // Optional<int32>: None on overflow
let wrapped = int32s.wrapping_add(largest, 1);       // wraps instead of trapping
let floor = int32s.floor_div(-7, 2);                 // -4
let hex = int32s.to_string_radix(255, 16);           // "ff"
let factorial = bigs.of_int64(20).multiply(bigs.of_int64(21));
```

Every sized number type has a companion module in `Std.Core`. The integer companions (`Int8` ... `Int64`, `UInt8` ... `UInt64`) have `Min`, `Max`, `min`, `max`, `clamp`, `compare`, `parse`, `to_string_radix`, and checked, wrapping, and saturating arithmetic; the signed ones also have `abs`. The float companions (`Float16`, `BFloat16`, `Float32`, `Float64`) have different members: `Max`, `MinPositive`, `MinNormal`, `Epsilon`, `Infinity`, `NaN`, `is_nan`, `abs`, `min`, `max`, `clamp`, `floor`, `ceil`, `trunc`, `round`, `total_compare` (not `compare`), `to_fixed`, `to_exponential`, and `parse`. Grep [stdlib-api.md](stdlib-api.md) before calling one. `Std.Math.BigInteger` and `BigDecimal` give arbitrary precision.

## Files and paths

<!-- example: check -->
```neura
let console = import:Std.Io.Console;
let error = import:Std.Core.Error;
let fs = import:Std.Fs.Fs;
let files = import:Std.Fs.File;
let path = import:Std.Fs.Path;
let result = import:Std.Core.Result;
let unit = import:Std.Core.Unit;

let copy_upper = func(source: string, target: string) -> result.Result<unit.Unit> {
    match (fs.read_text(path.of(source))) {
        result.Result.Ok(text) => {
            return fs.write_text(path.of(target), text.to_upper_case());
        },
        result.Result.Err(failure) => {
            return result.Result<unit.Unit>.Err(error.context(failure, "reading " + source));
        },
    }
};

let list_dir = func(directory: string) {
    match (fs.list(path.of(directory))) {
        result.Result.Ok(entries) => {
            for (let entry in entries) {
                console.print_line(entry.to_string());
            }
        },
        result.Result.Err(failure) => {
            console.eprintln("cannot list: {}", error.message(failure));
        },
    }
};

let first_bytes = func(location: string) -> result.Result<uint8[list]> {
    return files.with_open<uint8[list]>(path.of(location), files.OpenMode.Read,
        func(file: files.File) -> result.Result<uint8[list]> => file.read(16));   // closed for you
};
```

- `Std.Fs.Fs`: `read_text`, `read_bytes`, `write_text`, `write_bytes`, `append_text`, `exists`, `metadata`, `list`, `create_directory`, `create_directories`, `remove_file`, `remove_directory`, `remove_tree`, `rename`, `copy_file`. All return `Result`.
- `Std.Fs.Path`: `path.of(text)`, then `join`, `parent`, `file_name`, `stem`, `extension`, `components`, `relative_to`.
- `Std.Fs.File` opens handles (`OpenMode.Read`, `Write`, `Append`, `ReadWrite`, `CreateNew`) that you `close()`; `with_open` closes for you. `Std.Io.BufferedReader`, `BufferedWriter`, `Utf8Reader`, and `Utf8Writer` wrap byte sources and sinks.
- `void` results are `Result<Unit>`; `unit.Unit.Value` is the single value (`result.Result<unit.Unit>.Ok(unit.Unit.Value)`).

## JSON

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;
let json = import:Std.Encoding.Json;
let optional = import:Std.Core.Optional;
let result = import:Std.Core.Result;

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        match (json.parse("{\"name\": \"Ada\", \"tags\": [1, 2]}")) {
            result.Result.Ok(document) => {
                match (document) {
                    json.JsonValue.Object(members) => {
                        match (json.member(members, "name")) {
                            optional.Optional.Some(value) => {
                                console.print_line("name: " + json.write(value));
                            },
                            optional.Optional.None => {
                                console.print_line("no name");
                            },
                        }
                    },
                    _ => {
                        console.print_line("not an object");
                    },
                }
            },
            result.Result.Err(failure) => {
                return program.ExitCode.Failure;
            },
        }
        let members: [string, json.JsonValue][list] = [];
        members.append(["ok", json.JsonValue.Boolean(true)]);
        members.append(["count", json.number_of_int64(3)]);
        console.print_line(json.write(json.JsonValue.Object(members)));
        return program.ExitCode.Ok;
    };
);
```

```text
name: "Ada"
{"ok":true,"count":3}
```

`JsonValue` is `Null | Boolean(bool) | Number(string) | Text(string) | Array(JsonValue[list]) | Object([string, JsonValue][list])`. Numbers keep their text; convert with `number_to_int64` (an `Optional`) or `number_to_float64`. `write_pretty(value, indent)` indents. `Std.Encoding` also has `Utf8`, `Hex`, `Base64`, and `Csv`; `Std.Digest` has `Crc32` and `Sha256`.

## Time

<!-- example: check -->
```neura
let durations = import:Std.Time.Duration;
let system_clocks = import:Std.Time.SystemClock;
let stopwatches = import:Std.Time.Stopwatch;
let result = import:Std.Core.Result;

let timeout = durations.of_seconds(30).plus(durations.of_millis(500));
let millis = timeout.to_millis();

let wait_a_bit = func {
    let clock = system_clocks.create();
    let started = clock.monotonic();          // Result<Duration>
    let _ = clock.sleep(durations.of_millis(10));
    match (clock.now()) {                     // Result<Instant>: wall clock
        result.Result.Ok(instant) => { let text = instant.to_string(); },
        result.Result.Err(failure) => { },
    }
};
```

`Duration` (nanosecond precision) and `Instant` are classes with arithmetic methods; `LocalDate`, `LocalTime`, `LocalDateTime`, `ZoneOffset` (fixed offsets only, no time zones), and `DayOfWeek` cover calendars. `Clock` is an interface implemented by `SystemClock` and `FixedClock`; take a `Clock` parameter to make code testable. Reading the clock needs the clock capability.

## Randomness

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;
let randoms = import:Std.Random.Random;

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let rng = randoms.seeded(42);                  // deterministic xoshiro256**
        let first = rng.next_int_below(100);
        let again = randoms.seeded(42).next_int_below(100);
        console.println("{}", first == again);
        let deck: int[list] = [1, 2, 3, 4];
        rng.shuffle<int>(deck);
        console.println("{}", deck.length);
        return program.ExitCode.Ok;
    };
);
```

```text
true
4
```

`randoms.from_entropy()` returns `Result<Random>` seeded from the operating system (entropy capability). `Std.Random.Uuid` makes UUIDs. Methods: `next_uint64`, `next_uint32`, `next_bool`, `next_int_below`, `next_int_between`, `next_float64`, `next_gaussian`, `fill_bytes`, `shuffle<T>`, `choose<T>`.

## Environment, exit, and logging

<!-- example: check -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;
let loggers = import:Std.Log.Logger;

let home = func -> string {
    return match (program.Environment.get("HOME")) {
        program.Environment.Lookup.Found(value) => value,
        program.Environment.Lookup.Missing => "",
        program.Environment.Lookup.Failed(failure) => "",
    };
};

let log = loggers.create("app", func(line: string) { console.error_line(line); }, loggers.LogLevel.Info);

let stop = func {
    log.info("stopping");
    program.Process.exit(program.ExitCode.Failure);   // or Process.exit(3)
};
```

## Tests with Std.Test

A test is an exported, zero-argument, `void` function whose name starts with `test_`, in a module of a file named on the `neura test` command line.

<!-- example: test -->
```neura
let test = import:Std.Test;
let optional = import:Std.Core.Optional;
let result = import:Std.Core.Result;

let MathTests = module (
    export let test_arithmetic = func {
        test.assert_equal_int32(4, 2 + 2);
        test.assert_equal<string>("ab", "a" + "b");
        test.assert(10 % 3 == 1);
        test.assert_with(7 / 2 == 3, "integer division truncates");
        test.assert_float_near(0.3, 0.1 + 0.2, 0.000001);
    };

    export let test_optional_and_result = func {
        let value = test.assert_some<int>(optional.Optional<int>.Some(5));
        test.assert_equal_int32(5, value);
        test.assert_none<int>(optional.Optional<int>.None);
        let parsed = test.assert_ok<int32>(int32.parse("12"));
        let failure = test.assert_err<int32>(int32.parse("x"));
    };
);
```

Assertions: `assert`, `assert_with`, `fail`, `assert_equal_int32`, `assert_equal_int64`, `assert_equal_uint64`, `assert_equal_bool`, `assert_equal_string`, `assert_equal_float64`, `assert_float_near`, `assert_equal<T>`, `assert_not_equal<T>`, `assert_some<T>`, `assert_none<T>`, `assert_ok<T>`, `assert_err<T>`. A failure prints `FAIL Module.test_name: expected <5>, got <4> at file.nra:11:9` and the run continues with the next test. Each test starts from a fresh program state. There is no way to expect a trap inside a test; test trapping code with a separate program. See [cli.md](cli.md#test) for options.

## Module map

| Namespace | Modules |
| --- | --- |
| `Std.Core` | `Optional`, `Result`, `Error`, `Unit`, `Cell`, `Ordering`, `Comparable`, `Comparators`, `Hash`, `IntRange`, `Version`, `String`, `StringBuilder`, `Variadic`, `Failure`, `Bool`, `Int8`..`Int64`, `UInt8`..`UInt64`, `Float16`, `BFloat16`, `Float32`, `Float64` |
| `Std.Text` | `Strings`, `Scalars`, `Regex`, `Charsets` |
| `Std.Collections` | Interfaces `Iterator`, `Iterable`, `Collection`, `MutableCollection`, `List`, `MutableList`, `Set`, `MutableSet`, `SortedSet`, `Queue`, `Deque`, `Map`, `MutableMap`, `SortedMap`; classes `ArrayList`, `ArrayDeque`, `LinkedList`, `PriorityQueue`, `BitSet`, `OrdinalMap`, `HashMap`, `HashSet`, `IntMap`, `StringMap`, `TreeMap`, `TreeSet`, `ImmutableList`, `ImmutableSet`, `ImmutableMap`, `Sequence`; helpers `Sorting`, `Lists`, `Sets`, `Maps` |
| `Std.Math` | `Math`, `BigInteger`, `BigDecimal` |
| `Std.Time` | `Duration`, `Instant`, `Clock`, `SystemClock`, `FixedClock`, `Stopwatch`, `LocalDate`, `LocalTime`, `LocalDateTime`, `ZoneOffset`, `DayOfWeek` |
| `Std.Concurrent` | `Task`, `Scope`, `Channel`, `Cancellation`, `Timer`, `Tasks`, `WorkerGroup`, `Parallel`, `AtomicInt64`, `Semaphore`, `Guarded` |
| `Std.Random` | `Random`, `Uuid` |
| `Std.Encoding` | `Utf8`, `Hex`, `Base64`, `Csv`, `Json` |
| `Std.Digest` | `Crc32`, `Sha256` |
| `Std.Io` | `Console`, `Closeable`, `ByteBuffer`, `BufferedReader`, `BufferedWriter`, `Utf8Reader`, `Utf8Writer` |
| `Std.Fs` | `Path`, `File`, `Fs` |
| `Std.Net` | `TcpStream`, `TcpListener` |
| `Std.Program` | `Entry`, `ExitCode`, `Environment`, `Process` (nested in one module) |
| `Std.Log` | `Logger` |
| `Std.Test` | assertions |
| `Std.DynamicForeign` | `Arena`, `MemorySegment`, `MemoryLayout`, `ValueLayout`, `FunctionDescriptor`, `SymbolLookup`, `Linker`, `MethodHandle`, `ForeignValue` |

Not provided in this release: time zones, locale-sensitive formatting, HTTP, TLS, UDP, and concurrent collections.

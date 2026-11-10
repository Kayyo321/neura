# Standard library and I/O

The standard library is Neura source installed with the tool below `share/neura/stdlib` and resolved for every `Std.*` import without an `-I` option ([Modules](Modules.md#standard-library-imports)). It is compiled together with each program, and an image contains only the library code the program reaches. The [implementation plan](Stdlib.md) specifies its contracts and algorithms; [section 18](Stdlib.md#18-implementation-decisions) records the decisions made where the plan was open.

Each type has its own module, named after it: `Std.Collections.ArrayList` is `stdlib/std/collections/array-list.nra` and exports the class `ArrayList` and its module functions. Import each module you use; there is no prelude.

```neura
let array_list = import:Std.Collections.ArrayList;
let console = import:Std.Io.Console;
let program = import:Std.Program;

export let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let names = array_list.create<string>();
        names.add("Ada");
        for (let name in names) {
            console.print_line(name);
        }
        return program.ExitCode.Ok;
    };
);
```

| Area | Modules |
| --- | --- |
| `Std.Core` | `Failure`, `Unit`, `Cell`, `Ordering`, `Comparable`, `Comparators`, `Hash`, `Error`, `Optional`, `Result`, `IntRange`, `Version`, `StringBuilder`, `Variadic`, `Int8` … `Int64`, `UInt8` … `UInt64`, `Float16`, `BFloat16`, `Float32`, `Float64`, `Bool` |
| `Std.Text` | `Strings`, `Scalars`, `Regex` |
| `Std.Collections` | The interfaces `Iterator`, `Iterable`, `Collection`, `MutableCollection`, `List`, `MutableList`, `Set`, `MutableSet`, `SortedSet`, `Queue`, `Deque`, `Map`, `MutableMap`, `SortedMap`; `ArrayList`, `ArrayDeque`, `LinkedList`, `PriorityQueue`, `BitSet`, `OrdinalMap`, `HashMap`, `HashSet`, `IntMap`, `StringMap`, `TreeMap`, `TreeSet`, `ImmutableList`, `ImmutableSet`, `ImmutableMap`, `Sequence`, `Sorting`, `Lists`, `Sets`, `Maps` |
| `Std.Math` | `Math` (an fdlibm 5.3 port), `BigInteger`, `BigDecimal` |
| `Std.Time` | `Duration`, `Instant`, `Clock`, `SystemClock`, `FixedClock`, `Stopwatch`, `LocalDate`, `LocalTime`, `LocalDateTime`, `ZoneOffset`, `DayOfWeek` |
| `Std.Random` | `Random` (xoshiro256**), `Uuid` |
| `Std.Encoding` | `Utf8`, `Hex`, `Base64`, `Csv`, `Json` |
| `Std.Digest` | `Crc32`, `Sha256` |
| `Std.Io` | `Console`, `Closeable`, `ByteBuffer`, `BufferedReader`, `BufferedWriter`, `Utf8Reader`, `Utf8Writer` |
| `Std.Fs` | `Path`, `File`, `Fs` |
| `Std.Net` | `TcpStream`, `TcpListener` |
| `Std.Program` | `Entry`, `ExitCode`, `Environment`, `Process` |
| `Std.Log` | `Logger` |
| `Std.Test` | The assertions of [Testing](Testing.md) |

Expected failures are `Result<T>` values whose error is the `Error` enum; absent values are `Optional<T>`. A violated precondition traps with one of the messages of [section 5.1](Stdlib.md#51-failure-model).

`RegexFlags` compares and hashes its three option values. A compiled `Regex` uses its pattern and the options recorded at compilation, and its text is the pattern. A `RegexMatch` uses its input, capture offsets, and group names, and its text is the whole match. `FileMetadata` uses the entry kind, size, and modification time for equality and hashing. Its text is `Kind(size, modified)`, with `nil` when the modification time is absent. Mutating flags or metadata can change their hashes.

## Console formatting

`Console.format(pattern, args...)` and the static `Console.Formatter.format` return a UTF-8 string. `print` and `println` format to stdout, and `eprint` and `eprintln` format to stderr. The two newline forms append one LF. The module functions have `try_` counterparts that return a `Result` containing the string or `Unit`. For an existing `Std.Core.Variadic.Argument[]`, use `vformat` or `try_vformat`. Use `write`, `print_line`, or `error_line` for already-formatted text that may contain literal braces.

```neura
let console = import:Std.Io.Console;

console.println("{}, {}", "hello", "Neura");           // hello, Neura
console.println("{{{1}}}: {0:#06x}", 42, "value");     // {value}: 0x002a
let label = console.format("{:*^7} {:+.2f}", "ok", 1.25); // **ok*** +1.25
```

`{}` uses the next argument, and `{0}`, `{1}`, and so on use zero-based indexes. A pattern uses one indexing mode throughout. Explicit indexes may repeat and unused arguments are permitted. Write `{{` and `}}` for single literal braces. A colon in a field introduces its format specification.

| Option | Behavior |
| --- | --- |
| Fill and alignment | `<` left, `>` right, `^` center. An optional Unicode scalar before the alignment symbol replaces the default space. Text defaults to left alignment and numbers to right alignment. |
| Numeric sign and padding | `+` forces a sign, `-` is the default, and a space reserves the positive sign. `#` adds a radix prefix or forces a decimal point. `0` pads after the sign and radix prefix when alignment is implicit. |
| Width and precision | A decimal width sets a minimum field size. `.precision` limits text to that many Unicode scalars or controls decimal digits for floats. Width is measured in Unicode scalars. A nested field such as `{:>{}}` or `{0:.{1}f}` takes its width or precision from a nonnegative integer argument. |
| Integer presentation | `d` decimal, `b` or `B` binary, `o` octal, `x` or `X` hexadecimal, and `c` a Unicode scalar. Booleans also support the numeric presentations other than `c`. |
| Float presentation | `f` fixed, `e` scientific, `g` significant-digit formatting, and `a` hexadecimal. Uppercase variants capitalize the result. Decimal presentations default to six digits, while hexadecimal omits trailing zeros by default. All accept precision up to 100. The default presentation uses the argument type's shortest round-trip text when precision is omitted. |
| Text presentation | `s` inserts text. `?` quotes and escapes text. Objects use their `to_string()` method and a nil reference renders as `nil`. Booleans default to `true` and `false`. |

Format strings are checked at runtime and may come from any string expression. Invalid formats return an `Error.Parse` with a zero-based byte offset from the `try_` forms, or trap from the other forms. The library finishes formatting before sending anything to a stream, so a format error produces no partial output. Output uses the same host services and capability as raw console writes.

## Host services and capabilities

Everything that reaches outside the program goes through a named, versioned host service ([NVM](../runtime/NVM.md#host-services)). `neura run` and `neura test` allow the console and process exit by default; files, the network, environment variables, clocks, and entropy need `--allow-fs`, `--allow-net`, `--allow-env`, `--allow-clock`, and `--allow-entropy` ([Tooling](Tooling.md#build-and-run-executables)). Without the capability a call returns an `Io` error of kind `CapabilityDenied`. `neura run --record` and `--replay` capture and replay every host interaction, which makes programs that use time, entropy, files, or sockets reproducible.

A host resource (`File`, `TcpStream`, `TcpListener`) is closed explicitly with `close()`. When a run ends, the runner closes whatever the program left open; reclaiming an unreachable resource object does not close it.

## Testing the library

Each module's tests are in `tests/stdlib/<area>/` ([Contributing](../../CONTRIBUTING.md)): test files for `neura test`, trap programs with expected diagnostics, and programs with expected output. CTest runs every area against a fresh installation in optimized and unoptimized builds and checks repeated runs for identical output and step counts. `backend-reference` runs every library test function under the NeuraIR reference evaluator as well and compares outcomes with NVM. `tests/benchmarks` records step counts, peak heap cells, neuron counts, and image sizes for representative workloads against a checked-in baseline.

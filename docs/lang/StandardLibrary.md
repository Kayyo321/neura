# Standard library and I/O

The standard library is Neura source installed with the tool below `share/neura/stdlib` and resolved for every `Std.*` import without an `-I` option ([Modules](Modules.md#standard-library-imports)). It is compiled together with each program, and an image contains only the library code the program reaches. The [implementation plan](Stdlib.md) specifies its contracts and algorithms; [section 18](Stdlib.md#18-implementation-decisions) records the decisions made where the plan was open.

Each type has its own module, named after it: `Std.Collections.ArrayList` is `stdlib/std/collections/array-list.nra` and exports the class `ArrayList` and its module functions. Import each module you use; there is no prelude.

```neura
let array_list = import:Std.Collections.ArrayList;
let console = import:Std.Io.Console;
let program = import:Std.Program;

export let App = module:program.Entry (
    export let.override main = func(string[] args) -> program.ExitCode {
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
| `Std.Core` | `Failure`, `Unit`, `Cell`, `Ordering`, `Comparable`, `Comparators`, `Hash`, `Error`, `Optional`, `Result`, `IntRange`, `Version`, `StringBuilder`, `Int8` … `Int64`, `UInt8` … `UInt64`, `Float16`, `BFloat16`, `Float32`, `Float64`, `Bool` |
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

## Host services and capabilities

Everything that reaches outside the program goes through a named, versioned host service ([NVM](../runtime/NVM.md#host-services)). `neura run` and `neura test` allow the console and process exit by default; files, the network, environment variables, clocks, and entropy need `--allow-fs`, `--allow-net`, `--allow-env`, `--allow-clock`, and `--allow-entropy` ([Tooling](Tooling.md#build-and-run-executables)). Without the capability a call returns an `Io` error of kind `CapabilityDenied`. `neura run --record` and `--replay` capture and replay every host interaction, which makes programs that use time, entropy, files, or sockets reproducible.

A host resource (`File`, `TcpStream`, `TcpListener`) is closed explicitly with `close()`. When a run ends, the runner closes whatever the program left open; reclaiming an unreachable resource object does not close it.

## Testing the library

Each module's tests are in `tests/stdlib/<area>/` ([Contributing](../../CONTRIBUTING.md)): test files for `neura test`, trap programs with expected diagnostics, and programs with expected output. CTest runs every area against a fresh installation in optimized and unoptimized builds and checks repeated runs for identical output and step counts. `backend-reference` runs every library test function under the NeuraIR reference evaluator as well and compares outcomes with NVM. `tests/benchmarks` records step counts, peak heap cells, neuron counts, and image sizes for representative workloads against a checked-in baseline.

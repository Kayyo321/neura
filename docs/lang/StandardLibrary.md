# Standard library and I/O

The standard library is Neura source installed with the tool below `share/neura/stdlib` and resolved for every `Std.*` import without an `-I` option ([Modules](Modules.md#standard-library-imports)). It is compiled together with each program, and an image contains only the library code the program reaches. The [library reference](../../wiki/Stdlib.md) links to individual modules; the [specification](Stdlib.md) gives their contracts and algorithms, and [section 18](Stdlib.md#18-behavior-details) states the rules that complete them.

Each type has its own module, named after it: `Std.Collections.ArrayList` is `stdlib/std/collections/array-list.nra` and exports the class `ArrayList` and its module functions. Import each module you use; there is no prelude.

<!-- example: run -->
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

The program prints:

```text
Ada
```

| Area | Modules |
| --- | --- |
| `Std.Core` | `Failure`, `Unit`, `Cell`, `Ordering`, `Comparable`, `Comparators`, `Hash`, `Error`, `Optional`, `Result`, `IntRange`, `Version`, `String`, `StringBuilder`, `Variadic`, `Int8` … `Int64`, `UInt8` … `UInt64`, `Float16`, `BFloat16`, `Float32`, `Float64`, `Bool` |
| `Std.Text` | `Strings`, `Scalars`, `Regex`, `Charsets` |
| `Std.Collections` | The interfaces `Iterator`, `Iterable`, `Collection`, `MutableCollection`, `List`, `MutableList`, `Set`, `MutableSet`, `SortedSet`, `Queue`, `Deque`, `Map`, `MutableMap`, `SortedMap`; `ArrayList`, `ArrayDeque`, `LinkedList`, `PriorityQueue`, `BitSet`, `OrdinalMap`, `HashMap`, `HashSet`, `IntMap`, `StringMap`, `TreeMap`, `TreeSet`, `ImmutableList`, `ImmutableSet`, `ImmutableMap`, `Sequence`, `Sorting`, `Lists`, `Sets`, `Maps` |
| `Std.Math` | `Math` (an fdlibm 5.3 port), `BigInteger`, `BigDecimal` |
| `Std.Time` | `Duration`, `Instant`, `Clock`, `SystemClock`, `FixedClock`, `Stopwatch`, `LocalDate`, `LocalTime`, `LocalDateTime`, `ZoneOffset`, `DayOfWeek` |
| `Std.Concurrent` | `Task`, `Scope`, `Channel`, `Cancellation`, `Timer`, `Tasks`, `WorkerGroup`, `Parallel`, `AtomicInt64`, `Semaphore`, `Guarded` ([below](#stdconcurrent)) |
| `Std.Random` | `Random` (xoshiro256**), `Uuid` |
| `Std.Encoding` | `Utf8`, `Hex`, `Base64`, `Csv`, `Json` |
| `Std.Digest` | `Crc32`, `Sha256` |
| `Std.Io` | `Console`, `Closeable`, `ByteBuffer`, `BufferedReader`, `BufferedWriter`, `Utf8Reader`, `Utf8Writer` |
| `Std.Fs` | `Path`, `File`, `Fs` |
| `Std.Net` | `TcpStream`, `TcpListener` |
| `Std.Program` | `Entry`, `ExitCode`, `Environment`, `Process` |
| `Std.Log` | `Logger` |
| `Std.Test` | The assertions of [Testing](Testing.md) |
| `Std.DynamicForeign` | `Arena`, `MemorySegment`, `MemoryLayout`, `ValueLayout`, `FunctionDescriptor`, `SymbolLookup`, `Linker`, `MethodHandle`, `ForeignValue` ([Foreign interop](ForeignInterop.md)) |

Expected failures are `Result<T>` values whose error is the `Error` enum; absent values are `Optional<T>`. A violated precondition traps with one of the messages of [section 5.1](Stdlib.md#51-failure-model). An `Error` cannot be sent on a channel or given to a spawned task, because a `Custom` error holds an object. `Std.Core.Error.PortableError` is its transferable copy: `to_portable(e)` flattens a `Custom` error to its code and message, and `from_portable(p)` gives back an `Error` with the same message ([Stdlib](Stdlib.md#63-error)).

`RegexFlags` compares and hashes its three option values. A compiled `Regex` uses its pattern and the options recorded at compilation, and its text is the pattern. A `RegexMatch` uses its input, capture offsets, and group names, and its text is the whole match. `FileMetadata` uses the entry kind, size, and modification time for equality and hashing; its fields are read-only. Its text is `Kind(size, modified)`, with `nil` when the modification time is absent. Mutating flags can change their hashes.

## String

`Std.Core.String` is the companion module for the immutable `string` primitive. It inherits the receiver-first functions of [`Std.Text.Strings`](Stdlib.md#81-strings), including `contains`, `repeat`, UTF-8 construction, and ASCII case mapping. Import the module to call those functions explicitly or use the [primitive member syntax](Types.md#primitive-companions) on a string value.

Text positions and range lengths count UTF-8 bytes except in the explicit UTF-16 adapters. Use `byte_length` for bytes, `scalar_count` for Unicode scalar values, and `utf16_length` for UTF-16 code units. A slice includes its start and excludes its end. Byte slice endpoints must be on UTF-8 boundaries.

| Member | Behavior |
| --- | --- |
| `concat(s, suffix)`, `substring(s, start)`, `substring(s, start, end)`, `sub_sequence(s, start, end)` | Concatenation and checked UTF-8 byte slices. The one-offset form takes the rest of the string. |
| `content_equals(s, other)` | Compare contents against a string or a `StringBuilder`. A nil builder compares unequal. |
| `compare_to(s, other)`, `compare_to_ignore_ascii_case(s, other)` | Bytewise lexicographic comparison as -1, 0, or 1. The second form first folds ASCII letters to lowercase. |
| `starts_with_at(s, prefix, offset)`, `region_matches(s, start, other, other_start, byte_count)`, `region_matches_ignore_ascii_case(...)`, `region_matches_ignore_case(...)` | Compare regions and return false if an offset, length, or boundary is invalid. The last form uses the Unicode comparison described below. |
| `index_of_range(s, needle, start, end)`, `last_index_of_from(s, needle, from)` | Return a byte offset or -1. The first searches inside the checked range. The second searches backward from a valid UTF-8 boundary, including that boundary. |
| `index_of_code_point(s, scalar)`, `last_index_of_code_point(s, scalar)`, `replace_code_point(s, target, replacement)` | Search for or replace a Unicode scalar. Invalid scalar values trap. |
| `code_point_at(s, offset)`, `code_point_before(s, offset)`, `code_point_count(s, start, end)`, `offset_by_code_points(s, offset, count)` | Read or count Unicode scalars, or move a byte offset by a signed number of scalars. Invalid boundaries and moves outside the text trap. |
| `code_points(s)`, `get_bytes(s)` | Copy the Unicode scalars to a `uint32[list]` or the UTF-8 bytes to a `uint8[list]`. |
| `utf16_length(s)`, `char_at(s, index)`, `chars(s)`, `to_char_array(s)` | Count UTF-16 code units, read one code unit as `uint16`, or copy the units to a new `uint16[list]`. The two halves of a supplementary scalar can be read separately. Invalid indexes trap. |
| `get_chars(s, start, end, destination, destination_start)`, `from_char_array(units)` | Copy a UTF-16 code-unit range into an existing `uint16[]` or `uint16[list]`, or decode a `uint16[list]` into a string. Invalid ranges and unpaired surrogates trap. |
| `trim(s)`, `strip(s)`, `strip_leading(s)`, `strip_trailing(s)`, `is_blank(s)` | `trim` removes values through U+0020 at either end. The other operations use the whitespace definition below. An empty string is blank. |
| `lines(s)` | Split at CR, LF, or CRLF. The empty string gives no lines. A terminal separator contributes no extra line. |
| `indent(s, count)`, `strip_indent(s)` | `indent` adds spaces for positive counts or removes up to the magnitude in leading whitespace scalars for negative counts, then ends each line with LF. `strip_indent` removes the indentation common to nonblank lines and the final line, strips trailing whitespace from each line, and normalizes separators to LF. A terminal separator fixes the common indentation at zero. |
| `matches(s, pattern)`, `replace_all(s, pattern, replacement)`, `replace_first_regex(s, pattern, replacement)` | Compile with [`Std.Text.Regex`](Stdlib.md#84-regex). `matches` requires a whole-string match. An invalid pattern or replacement traps. |
| `split_regex(s, pattern)`, `split_regex_limit(s, pattern, limit)`, `split_with_delimiters(s, pattern, limit)` | Split at regular-expression matches. Limits must be positive and count pieces before interleaving matched delimiters. The last piece holds the remainder, and empty pieces are kept. |
| `formatted(s, args...)`, `vformatted(s, args)` | Use the [console format grammar](#console-formatting) and return the result as text. The second accepts a prebuilt `Argument[]`. Invalid formats trap. Neither writes to a stream. |
| `value_of<T>(value)`, `transform<T>(s, operation)` | Format a value that supports `to_string` or apply a callback of type `func(string) -> T`. |
| `translate_escapes(s)` | Translate escaped quotes, backslash, space, and the `b`, `f`, `n`, `r`, and `t` escapes. Also accepts octal escapes through `377` and removes backslash followed by CR, LF, or CRLF. Unknown or incomplete escapes trap. |

The whitespace set follows Java's `Character.isWhitespace` for strip and indentation. It includes U+0009–000D, U+001C–0020, U+1680, U+2000–2006, U+2008–200A, U+2028, U+2029, U+205F, and U+3000. The original `Std.Text.Strings.trim` and `Std.Text.Strings.lines` retain their own behavior. [Java SE 25 Character](https://docs.oracle.com/en/java/javase/25/docs/api/java.base/java/lang/Character.html#isWhitespace(int))

Neura strings always contain valid UTF-8. Hashes, ordinary comparisons, and byte indexes use that representation. The explicit UTF-16 adapters work in code units. Regular-expression and formatting operations use Neura's syntax. `split` and `replace` from `Std.Text.Strings` use literal matching. JVM object interning and constant descriptors have no direct equivalent. [Java SE 26 String](https://docs.oracle.com/en/java/javase/26/docs/api/java.base/java/lang/String.html)

### Unicode case and locale

The casing tables are pinned to Unicode 17.0.0. The no-locale forms of `to_lower_case` and `to_upper_case` use Unicode's root rules, including full mappings such as `"Straße".to_upper_case()` yielding `"STRASSE"` and the contextual Greek final sigma. The overloads accepting a language tag recognize the primary language `tr`, `az`, or `lt` without regard to ASCII case. These select the Turkish, Azerbaijani, and Lithuanian rules from Unicode SpecialCasing. Other tags select the root rules. Case conversion has no dependency on the machine's default locale. Locale-sensitive sorting and formatting are separate concerns. [Unicode casing FAQ](https://www.unicode.org/faq/casemap_charprop.html), [Unicode 17.0 SpecialCasing](https://www.unicode.org/Public/17.0.0/ucd/SpecialCasing.txt)

`equals_ignore_case` and `compare_to_ignore_case` use the simple lowercase of the simple uppercase of each scalar. The comparison orders those scalars lexicographically and returns -1, 0, or 1. `region_matches_ignore_case` applies the same rule to the two selected UTF-8 byte ranges.

`case_fold` uses the full, locale-independent Unicode mapping, which can expand a scalar to several scalars. `equals_fold_case` and `compare_to_fold_case` compare the resulting text. For example, `"Fuß".equals_fold_case("FUSS")` is true and `"Fuß".equals_ignore_case("FUSS")` is false. Normalization is a separate operation. [Unicode 17.0 CaseFolding](https://www.unicode.org/Public/17.0.0/ucd/CaseFolding.txt)

### Character encodings

`String.Charset` aliases the `Std.Text.Charsets.Charset` enum. It covers Java's standard encodings.

| Enum value | Name |
| --- | --- |
| `Ascii` | `US-ASCII` |
| `Latin1` | `ISO-8859-1` |
| `Utf8` | `UTF-8` |
| `Utf16`, `Utf16Be`, `Utf16Le` | `UTF-16`, `UTF-16BE`, `UTF-16LE` |
| `Utf32`, `Utf32Be`, `Utf32Le` | `UTF-32`, `UTF-32BE`, `UTF-32LE` |

`value.get_bytes()` returns a UTF-8 copy. Pass a charset value or a case-insensitive name to select another encoding. `string.decode_bytes(bytes)` decodes a `uint8[list]` as UTF-8. Its two-argument forms take a charset value or name. Common aliases include `ascii`, `latin1`, `utf8`, and the UTF-16 and UTF-32 names without hyphens. `Std.Text.Charsets.for_name` returns an error for an unsupported name. `get_bytes` and `decode_bytes` trap on an unsupported name.

When encoding to ASCII or Latin-1, an unrepresentable scalar becomes `?`. Decoding replaces each malformed unit or truncated tail with U+FFFD, using Unicode maximal subparts for UTF-8. `try_get_bytes` and `try_decode_bytes` return `Result` errors instead of replacing or trapping. An encoding error points to the first invalid byte offset in the input text or bytes. The inherited `from_bytes` remains the strict UTF-8 decoder.

The byte-order-aware `Utf16` and `Utf32` variants encode a big-endian byte-order mark at the start and read either byte order when a mark is present. They decode unmarked input as big-endian. The explicit-endian variants omit a mark when encoding and preserve a decoded U+FEFF as text. Charset conversion always returns ordinary UTF-8 text on decode. [Java SE 26 Charset](https://docs.oracle.com/en/java/javase/26/docs/api/java.base/java/nio/charset/Charset.html)

<!-- example: check -->
```neura
let lower = "I".to_lower_case("tr");              // ı
let bytes = lower.get_bytes("UTF-16LE");
let decoded = string.decode_bytes(bytes, "UTF-16LE");
```

## Console formatting

`Console.format(pattern, args...)` and the static `Console.Formatter.format` return a UTF-8 string. `print` and `println` format to stdout, and `eprint` and `eprintln` format to stderr. The two newline forms append one LF. The module functions have `try_` counterparts that return a `Result` containing the string or `Unit`. For an existing `Std.Core.Variadic.Argument[]`, use `vformat` or `try_vformat`. Use `write`, `print_line`, or `error_line` for already-formatted text that may contain literal braces.

<!-- example: check -->
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

## Std.Concurrent

`Std.Concurrent` holds the library side of [tasks](CoreLanguage.md#tasks-scopes-and-select): handles for the tasks and scopes that `spawn` and `scope` create, channels, cancellation, timers, helpers that run groups of calls, and handles for shared state. [Concurrency](Concurrency.md) is the specification, with the memory model and the scheduler. Each type has its own module, as elsewhere:

<!-- example: check -->
```neura
let channels = import:Std.Concurrent.Channel;

let [jobs, next_job, jobs_closer] = channels.open<int>(64);
```

| Module | Contract |
| --- | --- |
| `Task` | `Task<T>` is what `spawn` returns. `join()` waits until the task has finished and returns its result; joining again returns the same value, and a join is never cancelled. `is_finished()` never waits. A handle is scope-bound: it cannot leave the `scope` block that started its task. |
| `Scope` | The handle that `scope (name) { ... }` binds. `cancel()` requests cancellation of the scope's tasks and all their descendants; it is idempotent, cannot be undone, wakes their parked operations, and never interrupts a running task. `is_cancelled()` reports a request made by `cancel` or by the deadline. `cancel_after(duration)` and `cancel_at(instant)` set a deadline on the monotonic clock and return `Result<Unit>`, an `Err` of kind `CapabilityDenied` when the clock is denied. `deadline()` is the monotonic reading at which the tasks are cancelled, the earlier of the scope's own deadline and the one it inherited, as an `Optional<Duration>`. Scope-bound. |
| `Channel` | `open<T>(capacity)` returns `[Sender<T>, Receiver<T>, Closer<T>]`, the three ends of one bounded FIFO channel of a transferable `T`. Capacity 0 is a rendezvous; a negative capacity traps with `invalid argument: capacity`. `send(value)` parks while the buffer is full and returns `SendOutcome`: `Sent`, `Closed`, or `Cancelled`. `receive()` parks while the channel is empty and open and returns `Received<T>`: `Value(T)`, `Closed`, or `Cancelled`. `try_send` (`Sent`, `Full`, `Closed`) and `try_receive` (`Value(T)`, `Empty`, `Closed`) never park. `close()` is idempotent: later sends report `Closed`, parked senders wake with `Closed` and their values are not delivered, and receivers see `Closed` once the buffer is empty. |
| `Cancellation` | What the running task can ask about itself. `requested()` is whether cancellation was requested for the scope that started the task or for any scope above it; the root task is never cancelled. `remaining()` is the time to the task's deadline as an `Optional<Duration>`, negative once it has passed, and `None` when the task has no deadline or the clock cannot be read. |
| `Timer` | A one-shot timer on the monotonic clock. `after(duration)` and `at(instant)` return `Result<Timer>`, an `Err` when the clock is denied. `wait()` parks until the timer has fired and returns `TimerOutcome`: `Fired`, or `Cancelled` when the task's scope is cancelled first. A `wait` can be a `select` arm, which is how a select gives up on its other arms after a time. |
| `Tasks` | Helpers that run a list of calls as tasks and combine the results; a call is a function value of no arguments. `all<T>(calls) -> T[list]` returns the results in the order of the calls. `all_ok<T>(calls) -> Result<T[list]>` returns the values in call order when every call returns `Ok`, and otherwise the first `Err` in completion order. `first_ok<T>(calls) -> Result<T>` returns the first `Ok` in completion order, or the `Err` of the call that completed last. `all_ok` and `first_ok` cancel the calls still running, and every helper waits for all of its calls before it returns. An `Err` made by a helper has the code `tasks.cancelled` (the calling task was cancelled while it waited) or `tasks.empty` (`first_ok` of no calls). The helpers take and return lists, not arrays. |
| `WorkerGroup` | A fixed number of workers over a bounded queue. `with_workers<T>(workers, capacity, work, body)` starts the workers, runs `body` with the group's handle, then closes the queue, lets the workers finish every queued job, and returns once they have stopped. On the handle, `submit(job)` parks while the queue is full and returns `SubmitOutcome` (`Submitted`, `Closed`, `Cancelled`); `try_submit(job)` never parks and returns `TrySubmitOutcome` (`Submitted`, `Full`, `Closed`); `close_and_drain()` closes the queue and waits for the queued jobs. Fewer than one worker traps with `invalid argument: workers`, and a negative capacity with `invalid argument: capacity`. |
| `Parallel` | `map<T, U>(items: T[], worker: func(T) -> U, limits: ItemLimits) -> U[]` returns what `worker` returns for each item, in input order. Each item runs alone, in a heap of its own, under `limits(steps, heap_cells)`, both positive. The worker is task-safe, issues no host request, and uses no scheduler operation and no object identity; the compiler checks this. If items trap, the run reports the trap of the lowest index. `--parallel-workers` chooses how many threads evaluate items and changes only wall time and peak memory. |
| `AtomicInt64` | A shared signed 64-bit integer. `create(initial)`, `load()`, `store(value)`, `add(delta)`, which returns the sum and traps on overflow leaving the value unchanged, and `compare_exchange(expected, desired)`, which reports whether it stored. All operations on one atomic form a single order consistent with each task's program order. |
| `Semaphore` | A fixed number of permits. `create(permits)` traps with `invalid argument: permits` for a negative count. `acquire()` parks until a permit is free and returns `AcquireOutcome`: `Acquired` or `Cancelled`. `try_acquire()` returns a `bool` and never parks. `release()` returns a permit and wakes the task that has waited longest; releasing more permits than were created traps with `invalid state: released more permits than were created`. |
| `Guarded` | Mutable state that tasks share one at a time. `create<T>(initializer)` builds the state inside the guard. `with<U>(body)` runs `body` with exclusive access and returns its result; a second task that calls `with` parks until the first returns, and that wait is not cancellable. The captures of `initializer` and `body` and the result `U` are transferable, so no reference to the state escapes. Calling `with` on a guard the task already holds traps with `invalid state: guard already held`. |

- **Transferable handles.** Channel ends, `Timer`, `AtomicInt64`, `Semaphore`, `Guarded<T>`, and a `WorkerGroup<T>` handle can be given to a spawned task or sent on a channel. `Task<T>` and `Scope` cannot leave their scope. No other library class is transferable: a collection belongs to the task that created it.
- **Outcomes, not errors.** Closure and cancellation are expected events, so the operations above report them as enum values and return no `Error`.
- **Cancellation is cooperative.** It wakes a parked channel operation, timer wait, or cancellable host request with `Cancelled`. A task that is computing asks `Cancellation.requested()`.
- **The clock.** `cancel_after`, `cancel_at`, `Timer.after`, `Timer.at`, and `Cancellation.remaining` read the monotonic clock, which is granted by default and denied by `--deny-clock`. Under `--virtual-time` they read the virtual clock ([Testing](Testing.md#tests-of-concurrent-code)).
- **Native code.** A program that uses any scheduler operation cannot use `Std.DynamicForeign` or a C import (the compiler rejects the combination; [Concurrency, section 16](Concurrency.md#16-limits)).

## Task safety

A call that `spawn` starts must be [task-safe](Concurrency.md#52-task-safe-calls): nothing it reaches may read or write a reassignable module binding, read a module constant that is an object of a class that is not `#Immutable`, or call native code. `neura check --task-safety <files>` prints the answer for every function of the linked program, with the reason for each one that is not task-safe, so any function can be checked. A generic function is listed once for each specialization the program uses.

| Area | Task-safe | Not task-safe |
| --- | --- | --- |
| `Std.Core`, `Std.Text`, `Std.Math`, `Std.Random`, `Std.Log`, `Std.Program`, `Std.Test` | Every function | None |
| `Std.Collections` | Every function. The collections keep no module state, text conversion included. A generic member is checked with the callbacks and element types of each specialization, so a comparator or a hasher that is not task-safe makes the calls that use it unsafe. | None |
| `Std.Concurrent` | Every function | None |
| `Std.Fs`, `Std.Net` | Every function. They issue host requests, which a task may do. | None |
| `Std.Io` | Console output and formatting, `Closeable`, `ByteBuffer`, `BufferedWriter`, `Utf8Writer` | `Console.read_line` and the byte source that `Console.input()` returns: the read-ahead buffer belongs to the root task. In a program that calls `Console.input()`, also the read functions of `BufferedReader` (`read`, `read_exact`, `read_all`) and `Utf8Reader` (`read_scalar`, `read_line`, `read_to_end`), whatever source they were built from, because that source is a function value of the same type. A program that never calls `Console.input()` may use them in a task |
| `Std.Time` | Every function, `SystemClock`, `FixedClock`, and `Stopwatch` included. The value classes are `#Immutable`, so a task may be given their instances and may read `Duration.Zero`, `Instant.Epoch`, `LocalTime.Midnight`, and `ZoneOffset.Utc` | None |
| `Std.Encoding` | Every function. `Hex` and `Base64` run built-in kernels, which keep no module state | None |
| `Std.Digest` | Every function. Both digests run built-in kernels, which keep no module state, so a task or a parallel item can compute either one | None |
| `Std.DynamicForeign` | | Every function: it cannot be linked into a program with tasks |

The table is the report for a program that links every module of the library except `Std.DynamicForeign`. The `concurrency-frontend` test pins a smaller set: with `Console`, `HashMap`, `ArrayList`, `TreeMap`, `Float64`, `Duration`, `SystemClock`, `Instant`, `Logger`, `TcpStream`, `TcpListener`, `Strings`, `Regex`, `Random`, and `Json` linked, only the console's input functions may be listed. The rows for `Std.Encoding`, `Std.Digest`, the two readers of `Std.Io`, and the two `Std.Time` classes come from the report alone. A function that calls a function value is judged by every function value of that type the program can create, so a program that makes an unsafe function value of a type the library calls, such as a logger's sink or a comparator, adds the library functions that call it to the list.

A spawned task may read a `const` module binding of a transferable type, such as `Int32.Max` or `Duration.Zero`, whose class is `#Immutable`. It may not read a constant that is an object of another class: `BigInteger.Zero`, `One`, and `Ten`. It builds the value with the module's factory function, such as `of_int64`. The root task is not restricted.

The properties in this table that hold for every program are stated in the source with checked [tags](Tags.md): `#ThreadSafe` on the module functions of `Std.Core`, `Std.Math`, `Std.Text`, `Std.Encoding`, `Std.Digest`, and `Std.Time`, on the `Std.Concurrent` handles, and on `Console.print_line`, and `#Immutable` with `#ThreadSafe` on the value classes that tasks share ([Stdlib](Stdlib.md#58-task-safety)). A function whose task safety depends on a program's own overrides or function values, such as `Error.message` or `BufferedReader.read`, is not tagged.


## Host services and capabilities

Everything that reaches outside the program goes through a named, versioned host service ([NVM](../runtime/NVM.md#host-services)). `neura run` and `neura test` grant every host service by default: files, the network, environment variables, clocks, entropy, and native code work with no option. `--deny-fs`, `--deny-net`, `--deny-env`, `--deny-clock`, `--deny-entropy`, `--deny-native`, and `--deny-all` turn groups off ([Tooling](Tooling.md#build-and-run-executables)). A call to a denied service returns an `Io` error of kind `CapabilityDenied`. `neura run --record` and `--replay` capture and replay every host interaction, which makes programs that use time, entropy, files, or sockets reproducible.

A host resource (`File`, `TcpStream`, `TcpListener`) is closed explicitly with `close()`. When a run ends, the runner closes whatever the program left open; reclaiming an unreachable resource object does not close it. Two scoped helpers close for the program: `File.with_open<T>(path, mode, body)` and `TcpStream.with_connection<T>(host, port, body)` open the resource, run `body` with it, and close it exactly once when the body returns a value, returns an error, or returns because its scope was cancelled. If the open fails, the body is not called. If the close fails after a body `Ok`, the helper returns the close error; after a body `Err`, a `Context` error whose cause is the body's error. `Std.Io.Closeable.release<T>(resource, outcome)` applies the same rule to any `Closeable`. A trap in the body still ends the run without cleanup ([Stdlib](Stdlib.md#55-resources)).

In a program with tasks, the calls that wait on the outside world overlap: a task parked in `TcpStream.read`, `write`, or `connect`, `TcpListener.accept`, `Console.read_line`, or `SystemClock.sleep` lets the other tasks run. Cancelling its scope ends the wait with an `Io` error of kind `Cancelled`, unless the operation had already finished; a cancelled write reports the bytes it had written. File operations and console output complete before another task runs ([Concurrency](Concurrency.md#104-host-io)).

## Testing the library

Each module's tests are in `tests/stdlib/<area>/` ([Contributing](../../CONTRIBUTING.md)): test files for `neura test`, trap programs with expected diagnostics, and programs with expected output. CTest runs every area against a fresh installation in optimized and unoptimized builds and checks repeated runs for identical output and step counts. `backend-reference` runs every library test function under the NeuraIR reference evaluator as well and compares outcomes with NVM. `tests/benchmarks` records step counts, peak heap cells, neuron counts, and image sizes for representative workloads against a checked-in baseline. `tests/stdlib/concurrent` holds the tests of `Tasks` and `WorkerGroup`; the other `Std.Concurrent` modules are tested with the language forms under `tests/concurrency` and `tests/backend` ([Concurrency](Concurrency.md#15-verification)).

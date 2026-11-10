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
| `Std.Core` | `Failure`, `Unit`, `Cell`, `Ordering`, `Comparable`, `Comparators`, `Hash`, `Error`, `Optional`, `Result`, `IntRange`, `Version`, `String`, `StringBuilder`, `Variadic`, `Int8` … `Int64`, `UInt8` … `UInt64`, `Float16`, `BFloat16`, `Float32`, `Float64`, `Bool` |
| `Std.Text` | `Strings`, `Scalars`, `Regex`, `Charsets` |
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

```neura
let lower = "I".to_lower_case("tr");              // ı
let bytes = lower.get_bytes("UTF-16LE");
let decoded = string.decode_bytes(bytes, "UTF-16LE");
```

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

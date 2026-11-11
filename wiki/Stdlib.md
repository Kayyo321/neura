# Standard library reference

The Neura standard library contains source modules for core values, collections, text, arithmetic, time, input/output, host services, and concurrency. Select a module below for its contracts, source, and exported declarations. See the [standard library overview](StandardLibrary) for imports, string and console formatting, and runtime capabilities.

The [standard library specification](Stdlib-Design) gives the shared contracts. Language status and unresolved language decisions are in [feature status](Status).

## Modules

### `Std.Collections`

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.Collections.ArrayDeque`](Stdlib-Collections-ArrayDeque) | [Contracts](Stdlib-Collections-ArrayDeque#detailed-contracts) | [Declarations](Stdlib-Collections-ArrayDeque#exported-declarations) |
| [`Std.Collections.ArrayList`](Stdlib-Collections-ArrayList) | [Contracts](Stdlib-Collections-ArrayList#detailed-contracts) | [Declarations](Stdlib-Collections-ArrayList#exported-declarations) |
| [`Std.Collections.BitSet`](Stdlib-Collections-BitSet) | [Contracts](Stdlib-Collections-BitSet#detailed-contracts) | [Declarations](Stdlib-Collections-BitSet#exported-declarations) |
| [`Std.Collections.Collection`](Stdlib-Collections-Collection) | [Contracts](Stdlib-Collections-Collection#detailed-contracts) | [Declarations](Stdlib-Collections-Collection#exported-declarations) |
| [`Std.Collections.Deque`](Stdlib-Collections-Deque) | [Contracts](Stdlib-Collections-Deque#detailed-contracts) | [Declarations](Stdlib-Collections-Deque#exported-declarations) |
| [`Std.Collections.HashMap`](Stdlib-Collections-HashMap) | [Contracts](Stdlib-Collections-HashMap#detailed-contracts) | [Declarations](Stdlib-Collections-HashMap#exported-declarations) |
| [`Std.Collections.HashSet`](Stdlib-Collections-HashSet) | [Contracts](Stdlib-Collections-HashSet#detailed-contracts) | [Declarations](Stdlib-Collections-HashSet#exported-declarations) |
| [`Std.Collections.ImmutableList`](Stdlib-Collections-ImmutableList) | [Contracts](Stdlib-Collections-ImmutableList#detailed-contracts) | [Declarations](Stdlib-Collections-ImmutableList#exported-declarations) |
| [`Std.Collections.ImmutableMap`](Stdlib-Collections-ImmutableMap) | [Contracts](Stdlib-Collections-ImmutableMap#detailed-contracts) | [Declarations](Stdlib-Collections-ImmutableMap#exported-declarations) |
| [`Std.Collections.ImmutableSet`](Stdlib-Collections-ImmutableSet) | [Contracts](Stdlib-Collections-ImmutableSet#detailed-contracts) | [Declarations](Stdlib-Collections-ImmutableSet#exported-declarations) |
| [`Std.Collections.IntMap`](Stdlib-Collections-IntMap) | [Contracts](Stdlib-Collections-IntMap#detailed-contracts) | [Declarations](Stdlib-Collections-IntMap#exported-declarations) |
| [`Std.Collections.Iterable`](Stdlib-Collections-Iterable) | [Contracts](Stdlib-Collections-Iterable#detailed-contracts) | [Declarations](Stdlib-Collections-Iterable#exported-declarations) |
| [`Std.Collections.Iterator`](Stdlib-Collections-Iterator) | [Contracts](Stdlib-Collections-Iterator#detailed-contracts) | [Declarations](Stdlib-Collections-Iterator#exported-declarations) |
| [`Std.Collections.LinkedList`](Stdlib-Collections-LinkedList) | [Contracts](Stdlib-Collections-LinkedList#detailed-contracts) | [Declarations](Stdlib-Collections-LinkedList#exported-declarations) |
| [`Std.Collections.List`](Stdlib-Collections-List) | [Contracts](Stdlib-Collections-List#detailed-contracts) | [Declarations](Stdlib-Collections-List#exported-declarations) |
| [`Std.Collections.Lists`](Stdlib-Collections-Lists) | [Contracts](Stdlib-Collections-Lists#detailed-contracts) | [Declarations](Stdlib-Collections-Lists#exported-declarations) |
| [`Std.Collections.Map`](Stdlib-Collections-Map) | [Contracts](Stdlib-Collections-Map#detailed-contracts) | [Declarations](Stdlib-Collections-Map#exported-declarations) |
| [`Std.Collections.Maps`](Stdlib-Collections-Maps) | [Contracts](Stdlib-Collections-Maps#detailed-contracts) | [Declarations](Stdlib-Collections-Maps#exported-declarations) |
| [`Std.Collections.MutableCollection`](Stdlib-Collections-MutableCollection) | [Contracts](Stdlib-Collections-MutableCollection#detailed-contracts) | [Declarations](Stdlib-Collections-MutableCollection#exported-declarations) |
| [`Std.Collections.MutableList`](Stdlib-Collections-MutableList) | [Contracts](Stdlib-Collections-MutableList#detailed-contracts) | [Declarations](Stdlib-Collections-MutableList#exported-declarations) |
| [`Std.Collections.MutableMap`](Stdlib-Collections-MutableMap) | [Contracts](Stdlib-Collections-MutableMap#detailed-contracts) | [Declarations](Stdlib-Collections-MutableMap#exported-declarations) |
| [`Std.Collections.MutableSet`](Stdlib-Collections-MutableSet) | [Contracts](Stdlib-Collections-MutableSet#detailed-contracts) | [Declarations](Stdlib-Collections-MutableSet#exported-declarations) |
| [`Std.Collections.OrdinalMap`](Stdlib-Collections-OrdinalMap) | [Contracts](Stdlib-Collections-OrdinalMap#detailed-contracts) | [Declarations](Stdlib-Collections-OrdinalMap#exported-declarations) |
| [`Std.Collections.PriorityQueue`](Stdlib-Collections-PriorityQueue) | [Contracts](Stdlib-Collections-PriorityQueue#detailed-contracts) | [Declarations](Stdlib-Collections-PriorityQueue#exported-declarations) |
| [`Std.Collections.Queue`](Stdlib-Collections-Queue) | [Contracts](Stdlib-Collections-Queue#detailed-contracts) | [Declarations](Stdlib-Collections-Queue#exported-declarations) |
| [`Std.Collections.Sequence`](Stdlib-Collections-Sequence) | [Contracts](Stdlib-Collections-Sequence#detailed-contracts) | [Declarations](Stdlib-Collections-Sequence#exported-declarations) |
| [`Std.Collections.Set`](Stdlib-Collections-Set) | [Contracts](Stdlib-Collections-Set#detailed-contracts) | [Declarations](Stdlib-Collections-Set#exported-declarations) |
| [`Std.Collections.Sets`](Stdlib-Collections-Sets) | [Contracts](Stdlib-Collections-Sets#detailed-contracts) | [Declarations](Stdlib-Collections-Sets#exported-declarations) |
| [`Std.Collections.SortedMap`](Stdlib-Collections-SortedMap) | [Contracts](Stdlib-Collections-SortedMap#detailed-contracts) | [Declarations](Stdlib-Collections-SortedMap#exported-declarations) |
| [`Std.Collections.SortedSet`](Stdlib-Collections-SortedSet) | [Contracts](Stdlib-Collections-SortedSet#detailed-contracts) | [Declarations](Stdlib-Collections-SortedSet#exported-declarations) |
| [`Std.Collections.Sorting`](Stdlib-Collections-Sorting) | [Contracts](Stdlib-Collections-Sorting#detailed-contracts) | [Declarations](Stdlib-Collections-Sorting#exported-declarations) |
| [`Std.Collections.StringMap`](Stdlib-Collections-StringMap) | [Contracts](Stdlib-Collections-StringMap#detailed-contracts) | [Declarations](Stdlib-Collections-StringMap#exported-declarations) |
| [`Std.Collections.TreeMap`](Stdlib-Collections-TreeMap) | [Contracts](Stdlib-Collections-TreeMap#detailed-contracts) | [Declarations](Stdlib-Collections-TreeMap#exported-declarations) |
| [`Std.Collections.TreeSet`](Stdlib-Collections-TreeSet) | [Contracts](Stdlib-Collections-TreeSet#detailed-contracts) | [Declarations](Stdlib-Collections-TreeSet#exported-declarations) |

### `Std.Concurrent`

Tasks, channels, cancellation, timers, and shared-state handles. The [concurrency specification](Concurrency) defines the language forms these modules serve, and the [overview](StandardLibrary#stdconcurrent) lists their contracts side by side.

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.Concurrent.AtomicInt64`](Stdlib-Concurrent-AtomicInt64) | [Contracts](Stdlib-Concurrent-AtomicInt64#detailed-contracts) | [Declarations](Stdlib-Concurrent-AtomicInt64#exported-declarations) |
| [`Std.Concurrent.Cancellation`](Stdlib-Concurrent-Cancellation) | [Contracts](Stdlib-Concurrent-Cancellation#detailed-contracts) | [Declarations](Stdlib-Concurrent-Cancellation#exported-declarations) |
| [`Std.Concurrent.Channel`](Stdlib-Concurrent-Channel) | [Contracts](Stdlib-Concurrent-Channel#detailed-contracts) | [Declarations](Stdlib-Concurrent-Channel#exported-declarations) |
| [`Std.Concurrent.Guarded`](Stdlib-Concurrent-Guarded) | [Contracts](Stdlib-Concurrent-Guarded#detailed-contracts) | [Declarations](Stdlib-Concurrent-Guarded#exported-declarations) |
| [`Std.Concurrent.Parallel`](Stdlib-Concurrent-Parallel) | [Contracts](Stdlib-Concurrent-Parallel#detailed-contracts) | [Declarations](Stdlib-Concurrent-Parallel#exported-declarations) |
| [`Std.Concurrent.Scope`](Stdlib-Concurrent-Scope) | [Contracts](Stdlib-Concurrent-Scope#detailed-contracts) | [Declarations](Stdlib-Concurrent-Scope#exported-declarations) |
| [`Std.Concurrent.Semaphore`](Stdlib-Concurrent-Semaphore) | [Contracts](Stdlib-Concurrent-Semaphore#detailed-contracts) | [Declarations](Stdlib-Concurrent-Semaphore#exported-declarations) |
| [`Std.Concurrent.Task`](Stdlib-Concurrent-Task) | [Contracts](Stdlib-Concurrent-Task#detailed-contracts) | [Declarations](Stdlib-Concurrent-Task#exported-declarations) |
| [`Std.Concurrent.Tasks`](Stdlib-Concurrent-Tasks) | [Contracts](Stdlib-Concurrent-Tasks#detailed-contracts) | [Declarations](Stdlib-Concurrent-Tasks#exported-declarations) |
| [`Std.Concurrent.Timer`](Stdlib-Concurrent-Timer) | [Contracts](Stdlib-Concurrent-Timer#detailed-contracts) | [Declarations](Stdlib-Concurrent-Timer#exported-declarations) |
| [`Std.Concurrent.WorkerGroup`](Stdlib-Concurrent-WorkerGroup) | [Contracts](Stdlib-Concurrent-WorkerGroup#detailed-contracts) | [Declarations](Stdlib-Concurrent-WorkerGroup#exported-declarations) |

### `Std.Core`

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.Core.BFloat16`](Stdlib-Core-BFloat16) | [Contracts](Stdlib-Core-BFloat16#detailed-contracts) | [Declarations](Stdlib-Core-BFloat16#exported-declarations) |
| [`Std.Core.Bool`](Stdlib-Core-Bool) | [Contracts](Stdlib-Core-Bool#detailed-contracts) | [Declarations](Stdlib-Core-Bool#exported-declarations) |
| [`Std.Core.Cell`](Stdlib-Core-Cell) | [Contracts](Stdlib-Core-Cell#detailed-contracts) | [Declarations](Stdlib-Core-Cell#exported-declarations) |
| [`Std.Core.Comparable`](Stdlib-Core-Comparable) | [Contracts](Stdlib-Core-Comparable#detailed-contracts) | [Declarations](Stdlib-Core-Comparable#exported-declarations) |
| [`Std.Core.Comparators`](Stdlib-Core-Comparators) | [Contracts](Stdlib-Core-Comparators#detailed-contracts) | [Declarations](Stdlib-Core-Comparators#exported-declarations) |
| [`Std.Core.Error`](Stdlib-Core-Error) | [Contracts](Stdlib-Core-Error#detailed-contracts) | [Declarations](Stdlib-Core-Error#exported-declarations) |
| [`Std.Core.Failure`](Stdlib-Core-Failure) | [Contracts](Stdlib-Core-Failure#detailed-contracts) | [Declarations](Stdlib-Core-Failure#exported-declarations) |
| [`Std.Core.Float16`](Stdlib-Core-Float16) | [Contracts](Stdlib-Core-Float16#detailed-contracts) | [Declarations](Stdlib-Core-Float16#exported-declarations) |
| [`Std.Core.Float32`](Stdlib-Core-Float32) | [Contracts](Stdlib-Core-Float32#detailed-contracts) | [Declarations](Stdlib-Core-Float32#exported-declarations) |
| [`Std.Core.Float64`](Stdlib-Core-Float64) | [Contracts](Stdlib-Core-Float64#detailed-contracts) | [Declarations](Stdlib-Core-Float64#exported-declarations) |
| [`Std.Core.Hash`](Stdlib-Core-Hash) | [Contracts](Stdlib-Core-Hash#detailed-contracts) | [Declarations](Stdlib-Core-Hash#exported-declarations) |
| [`Std.Core.Int16`](Stdlib-Core-Int16) | [Contracts](Stdlib-Core-Int16#detailed-contracts) | [Declarations](Stdlib-Core-Int16#exported-declarations) |
| [`Std.Core.Int32`](Stdlib-Core-Int32) | [Contracts](Stdlib-Core-Int32#detailed-contracts) | [Declarations](Stdlib-Core-Int32#exported-declarations) |
| [`Std.Core.Int64`](Stdlib-Core-Int64) | [Contracts](Stdlib-Core-Int64#detailed-contracts) | [Declarations](Stdlib-Core-Int64#exported-declarations) |
| [`Std.Core.Int8`](Stdlib-Core-Int8) | [Contracts](Stdlib-Core-Int8#detailed-contracts) | [Declarations](Stdlib-Core-Int8#exported-declarations) |
| [`Std.Core.IntRange`](Stdlib-Core-IntRange) | [Contracts](Stdlib-Core-IntRange#detailed-contracts) | [Declarations](Stdlib-Core-IntRange#exported-declarations) |
| [`Std.Core.Optional`](Stdlib-Core-Optional) | [Contracts](Stdlib-Core-Optional#detailed-contracts) | [Declarations](Stdlib-Core-Optional#exported-declarations) |
| [`Std.Core.Ordering`](Stdlib-Core-Ordering) | [Contracts](Stdlib-Core-Ordering#detailed-contracts) | [Declarations](Stdlib-Core-Ordering#exported-declarations) |
| [`Std.Core.Result`](Stdlib-Core-Result) | [Contracts](Stdlib-Core-Result#detailed-contracts) | [Declarations](Stdlib-Core-Result#exported-declarations) |
| [`Std.Core.String`](Stdlib-Core-String) | [Contracts](Stdlib-Core-String#detailed-contracts) | [Declarations](Stdlib-Core-String#exported-declarations) |
| [`Std.Core.StringBuilder`](Stdlib-Core-StringBuilder) | [Contracts](Stdlib-Core-StringBuilder#detailed-contracts) | [Declarations](Stdlib-Core-StringBuilder#exported-declarations) |
| [`Std.Core.UInt16`](Stdlib-Core-UInt16) | [Contracts](Stdlib-Core-UInt16#detailed-contracts) | [Declarations](Stdlib-Core-UInt16#exported-declarations) |
| [`Std.Core.UInt32`](Stdlib-Core-UInt32) | [Contracts](Stdlib-Core-UInt32#detailed-contracts) | [Declarations](Stdlib-Core-UInt32#exported-declarations) |
| [`Std.Core.UInt64`](Stdlib-Core-UInt64) | [Contracts](Stdlib-Core-UInt64#detailed-contracts) | [Declarations](Stdlib-Core-UInt64#exported-declarations) |
| [`Std.Core.UInt8`](Stdlib-Core-UInt8) | [Contracts](Stdlib-Core-UInt8#detailed-contracts) | [Declarations](Stdlib-Core-UInt8#exported-declarations) |
| [`Std.Core.Unit`](Stdlib-Core-Unit) | [Contracts](Stdlib-Core-Unit#detailed-contracts) | [Declarations](Stdlib-Core-Unit#exported-declarations) |
| [`Std.Core.Variadic`](Stdlib-Core-Variadic) | [Contracts](Stdlib-Core-Variadic#detailed-contracts) | [Declarations](Stdlib-Core-Variadic#exported-declarations) |
| [`Std.Core.Version`](Stdlib-Core-Version) | [Contracts](Stdlib-Core-Version#detailed-contracts) | [Declarations](Stdlib-Core-Version#exported-declarations) |

### `Std.Digest`

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.Digest.Crc32`](Stdlib-Digest-Crc32) | [Contracts](Stdlib-Digest-Crc32#detailed-contracts) | [Declarations](Stdlib-Digest-Crc32#exported-declarations) |
| [`Std.Digest.Sha256`](Stdlib-Digest-Sha256) | [Contracts](Stdlib-Digest-Sha256#detailed-contracts) | [Declarations](Stdlib-Digest-Sha256#exported-declarations) |

### `Std.DynamicForeign`

Native libraries, memory, and calls; see [Foreign interop](ForeignInterop). Granted by default; denied with `--deny-native`.

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.DynamicForeign.Arena`](Stdlib-DynamicForeign-Arena) | [Contracts](Stdlib-DynamicForeign-Arena#detailed-contracts) | [Declarations](Stdlib-DynamicForeign-Arena#exported-declarations) |
| [`Std.DynamicForeign.ForeignValue`](Stdlib-DynamicForeign-ForeignValue) | [Contracts](Stdlib-DynamicForeign-ForeignValue#detailed-contracts) | [Declarations](Stdlib-DynamicForeign-ForeignValue#exported-declarations) |
| [`Std.DynamicForeign.FunctionDescriptor`](Stdlib-DynamicForeign-FunctionDescriptor) | [Contracts](Stdlib-DynamicForeign-FunctionDescriptor#detailed-contracts) | [Declarations](Stdlib-DynamicForeign-FunctionDescriptor#exported-declarations) |
| [`Std.DynamicForeign.Linker`](Stdlib-DynamicForeign-Linker) | [Contracts](Stdlib-DynamicForeign-Linker#detailed-contracts) | [Declarations](Stdlib-DynamicForeign-Linker#exported-declarations) |
| [`Std.DynamicForeign.MemoryLayout`](Stdlib-DynamicForeign-MemoryLayout) | [Contracts](Stdlib-DynamicForeign-MemoryLayout#detailed-contracts) | [Declarations](Stdlib-DynamicForeign-MemoryLayout#exported-declarations) |
| [`Std.DynamicForeign.MemorySegment`](Stdlib-DynamicForeign-MemorySegment) | [Contracts](Stdlib-DynamicForeign-MemorySegment#detailed-contracts) | [Declarations](Stdlib-DynamicForeign-MemorySegment#exported-declarations) |
| [`Std.DynamicForeign.MethodHandle`](Stdlib-DynamicForeign-MethodHandle) | [Contracts](Stdlib-DynamicForeign-MethodHandle#detailed-contracts) | [Declarations](Stdlib-DynamicForeign-MethodHandle#exported-declarations) |
| [`Std.DynamicForeign.SymbolLookup`](Stdlib-DynamicForeign-SymbolLookup) | [Contracts](Stdlib-DynamicForeign-SymbolLookup#detailed-contracts) | [Declarations](Stdlib-DynamicForeign-SymbolLookup#exported-declarations) |
| [`Std.DynamicForeign.ValueLayout`](Stdlib-DynamicForeign-ValueLayout) | [Contracts](Stdlib-DynamicForeign-ValueLayout#detailed-contracts) | [Declarations](Stdlib-DynamicForeign-ValueLayout#exported-declarations) |

### `Std.Encoding`

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.Encoding.Base64`](Stdlib-Encoding-Base64) | [Contracts](Stdlib-Encoding-Base64#detailed-contracts) | [Declarations](Stdlib-Encoding-Base64#exported-declarations) |
| [`Std.Encoding.Csv`](Stdlib-Encoding-Csv) | [Contracts](Stdlib-Encoding-Csv#detailed-contracts) | [Declarations](Stdlib-Encoding-Csv#exported-declarations) |
| [`Std.Encoding.Hex`](Stdlib-Encoding-Hex) | [Contracts](Stdlib-Encoding-Hex#detailed-contracts) | [Declarations](Stdlib-Encoding-Hex#exported-declarations) |
| [`Std.Encoding.Json`](Stdlib-Encoding-Json) | [Contracts](Stdlib-Encoding-Json#detailed-contracts) | [Declarations](Stdlib-Encoding-Json#exported-declarations) |
| [`Std.Encoding.Utf8`](Stdlib-Encoding-Utf8) | [Contracts](Stdlib-Encoding-Utf8#detailed-contracts) | [Declarations](Stdlib-Encoding-Utf8#exported-declarations) |

### `Std.Fs`

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.Fs.File`](Stdlib-Fs-File) | [Contracts](Stdlib-Fs-File#detailed-contracts) | [Declarations](Stdlib-Fs-File#exported-declarations) |
| [`Std.Fs.Fs`](Stdlib-Fs-Fs) | [Contracts](Stdlib-Fs-Fs#detailed-contracts) | [Declarations](Stdlib-Fs-Fs#exported-declarations) |
| [`Std.Fs.Path`](Stdlib-Fs-Path) | [Contracts](Stdlib-Fs-Path#detailed-contracts) | [Declarations](Stdlib-Fs-Path#exported-declarations) |

### `Std.Io`

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.Io.BufferedReader`](Stdlib-Io-BufferedReader) | [Contracts](Stdlib-Io-BufferedReader#detailed-contracts) | [Declarations](Stdlib-Io-BufferedReader#exported-declarations) |
| [`Std.Io.BufferedWriter`](Stdlib-Io-BufferedWriter) | [Contracts](Stdlib-Io-BufferedWriter#detailed-contracts) | [Declarations](Stdlib-Io-BufferedWriter#exported-declarations) |
| [`Std.Io.ByteBuffer`](Stdlib-Io-ByteBuffer) | [Contracts](Stdlib-Io-ByteBuffer#detailed-contracts) | [Declarations](Stdlib-Io-ByteBuffer#exported-declarations) |
| [`Std.Io.Closeable`](Stdlib-Io-Closeable) | [Contracts](Stdlib-Io-Closeable#detailed-contracts) | [Declarations](Stdlib-Io-Closeable#exported-declarations) |
| [`Std.Io.Console`](Stdlib-Io-Console) | [Contracts](Stdlib-Io-Console#detailed-contracts) | [Declarations](Stdlib-Io-Console#exported-declarations) |
| [`Std.Io.Utf8Reader`](Stdlib-Io-Utf8Reader) | [Contracts](Stdlib-Io-Utf8Reader#detailed-contracts) | [Declarations](Stdlib-Io-Utf8Reader#exported-declarations) |
| [`Std.Io.Utf8Writer`](Stdlib-Io-Utf8Writer) | [Contracts](Stdlib-Io-Utf8Writer#detailed-contracts) | [Declarations](Stdlib-Io-Utf8Writer#exported-declarations) |

### `Std.Log`

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.Log.Logger`](Stdlib-Log-Logger) | [Contracts](Stdlib-Log-Logger#detailed-contracts) | [Declarations](Stdlib-Log-Logger#exported-declarations) |

### `Std.Math`

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.Math.BigDecimal`](Stdlib-Math-BigDecimal) | [Contracts](Stdlib-Math-BigDecimal#detailed-contracts) | [Declarations](Stdlib-Math-BigDecimal#exported-declarations) |
| [`Std.Math.BigInteger`](Stdlib-Math-BigInteger) | [Contracts](Stdlib-Math-BigInteger#detailed-contracts) | [Declarations](Stdlib-Math-BigInteger#exported-declarations) |
| [`Std.Math.Math`](Stdlib-Math-Math) | [Contracts](Stdlib-Math-Math#detailed-contracts) | [Declarations](Stdlib-Math-Math#exported-declarations) |

### `Std.Net`

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.Net.TcpListener`](Stdlib-Net-TcpListener) | [Contracts](Stdlib-Net-TcpListener#detailed-contracts) | [Declarations](Stdlib-Net-TcpListener#exported-declarations) |
| [`Std.Net.TcpStream`](Stdlib-Net-TcpStream) | [Contracts](Stdlib-Net-TcpStream#detailed-contracts) | [Declarations](Stdlib-Net-TcpStream#exported-declarations) |

### `Std.Program`

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.Program`](Stdlib-Program) | [Contracts](Stdlib-Program#detailed-contracts) | [Declarations](Stdlib-Program#exported-declarations) |

### `Std.Random`

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.Random.Random`](Stdlib-Random-Random) | [Contracts](Stdlib-Random-Random#detailed-contracts) | [Declarations](Stdlib-Random-Random#exported-declarations) |
| [`Std.Random.Uuid`](Stdlib-Random-Uuid) | [Contracts](Stdlib-Random-Uuid#detailed-contracts) | [Declarations](Stdlib-Random-Uuid#exported-declarations) |

### `Std.Test`

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.Test`](Stdlib-Test) | [Contracts](Stdlib-Test#detailed-contracts) | [Declarations](Stdlib-Test#exported-declarations) |

### `Std.Text`

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.Text.Charsets`](Stdlib-Text-Charsets) | [Contracts](Stdlib-Text-Charsets#detailed-contracts) | [Declarations](Stdlib-Text-Charsets#exported-declarations) |
| [`Std.Text.Regex`](Stdlib-Text-Regex) | [Contracts](Stdlib-Text-Regex#detailed-contracts) | [Declarations](Stdlib-Text-Regex#exported-declarations) |
| [`Std.Text.Scalars`](Stdlib-Text-Scalars) | [Contracts](Stdlib-Text-Scalars#detailed-contracts) | [Declarations](Stdlib-Text-Scalars#exported-declarations) |
| [`Std.Text.Strings`](Stdlib-Text-Strings) | [Contracts](Stdlib-Text-Strings#detailed-contracts) | [Declarations](Stdlib-Text-Strings#exported-declarations) |

### `Std.Time`

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.Time.Clock`](Stdlib-Time-Clock) | [Contracts](Stdlib-Time-Clock#detailed-contracts) | [Declarations](Stdlib-Time-Clock#exported-declarations) |
| [`Std.Time.DayOfWeek`](Stdlib-Time-DayOfWeek) | [Contracts](Stdlib-Time-DayOfWeek#detailed-contracts) | [Declarations](Stdlib-Time-DayOfWeek#exported-declarations) |
| [`Std.Time.Duration`](Stdlib-Time-Duration) | [Contracts](Stdlib-Time-Duration#detailed-contracts) | [Declarations](Stdlib-Time-Duration#exported-declarations) |
| [`Std.Time.FixedClock`](Stdlib-Time-FixedClock) | [Contracts](Stdlib-Time-FixedClock#detailed-contracts) | [Declarations](Stdlib-Time-FixedClock#exported-declarations) |
| [`Std.Time.Instant`](Stdlib-Time-Instant) | [Contracts](Stdlib-Time-Instant#detailed-contracts) | [Declarations](Stdlib-Time-Instant#exported-declarations) |
| [`Std.Time.LocalDate`](Stdlib-Time-LocalDate) | [Contracts](Stdlib-Time-LocalDate#detailed-contracts) | [Declarations](Stdlib-Time-LocalDate#exported-declarations) |
| [`Std.Time.LocalDateTime`](Stdlib-Time-LocalDateTime) | [Contracts](Stdlib-Time-LocalDateTime#detailed-contracts) | [Declarations](Stdlib-Time-LocalDateTime#exported-declarations) |
| [`Std.Time.LocalTime`](Stdlib-Time-LocalTime) | [Contracts](Stdlib-Time-LocalTime#detailed-contracts) | [Declarations](Stdlib-Time-LocalTime#exported-declarations) |
| [`Std.Time.Stopwatch`](Stdlib-Time-Stopwatch) | [Contracts](Stdlib-Time-Stopwatch#detailed-contracts) | [Declarations](Stdlib-Time-Stopwatch#exported-declarations) |
| [`Std.Time.SystemClock`](Stdlib-Time-SystemClock) | [Contracts](Stdlib-Time-SystemClock#detailed-contracts) | [Declarations](Stdlib-Time-SystemClock#exported-declarations) |
| [`Std.Time.ZoneOffset`](Stdlib-Time-ZoneOffset) | [Contracts](Stdlib-Time-ZoneOffset#detailed-contracts) | [Declarations](Stdlib-Time-ZoneOffset#exported-declarations) |

## Specification sections

The [complete specification](Stdlib-Design) has these sections.

- [1. How to read this page](Stdlib-Design#1-how-to-read-this-page)
- [2. Principles](Stdlib-Design#2-principles)
- [3. Language facilities](Stdlib-Design#3-language-facilities)
  - [G1. Function types over type parameters](Stdlib-Design#g1-function-types-over-type-parameters)
  - [G2. Generic enums](Stdlib-Design#g2-generic-enums)
  - [G3. Universal members](Stdlib-Design#g3-universal-members)
  - [G4. Interfaces](Stdlib-Design#g4-interfaces)
  - [G5. For-each over library collections](Stdlib-Design#g5-for-each-over-library-collections)
  - [T1. Text primitives](Stdlib-Design#t1-text-primitives)
  - [N1. Float bit access](Stdlib-Design#n1-float-bit-access)
  - [F1. Trap with a message](Stdlib-Design#f1-trap-with-a-message)
  - [P1. Library root](Stdlib-Design#p1-library-root)
  - [P2. Host bindings in source](Stdlib-Design#p2-host-bindings-in-source)
  - [P2a. Kernel bindings in source](Stdlib-Design#p2a-kernel-bindings-in-source)
  - [P3. Entry point from the library](Stdlib-Design#p3-entry-point-from-the-library)
  - [What the language does not provide](Stdlib-Design#what-the-language-does-not-provide)
- [4. Architecture](Stdlib-Design#4-architecture)
  - [4.1 Modules](Stdlib-Design#41-modules)
  - [4.2 Source layout](Stdlib-Design#42-source-layout)
  - [4.3 Dependency order](Stdlib-Design#43-dependency-order)
  - [4.4 Built-in floor](Stdlib-Design#44-built-in-floor)
  - [4.5 Host services](Stdlib-Design#45-host-services)
  - [4.6 Retired built-ins](Stdlib-Design#46-retired-built-ins)
  - [4.7 Versioning](Stdlib-Design#47-versioning)
- [5. Core contracts](Stdlib-Design#5-core-contracts)
  - [5.1 Failure model](Stdlib-Design#51-failure-model)
  - [5.2 Equality, hashing, and text](Stdlib-Design#52-equality-hashing-and-text)
  - [5.3 Ordering](Stdlib-Design#53-ordering)
  - [5.4 Iteration](Stdlib-Design#54-iteration)
  - [5.5 Resources](Stdlib-Design#55-resources)
  - [5.6 Callbacks](Stdlib-Design#56-callbacks)
  - [5.7 Conformance summary](Stdlib-Design#57-conformance-summary)
  - [5.8 Task safety](Stdlib-Design#58-task-safety)
- [6. `Std.Core` types](Stdlib-Design#6-stdcore-types)
  - [6.1 `Optional<T>`](Stdlib-Design#61-optionalt)
  - [6.2 `Result<T>`](Stdlib-Design#62-resultt)
  - [6.3 `Error`](Stdlib-Design#63-error)
  - [6.4 Concrete aliases and absence](Stdlib-Design#64-concrete-aliases-and-absence)
  - [6.5 Small types](Stdlib-Design#65-small-types)
- [7. Primitive companion modules](Stdlib-Design#7-primitive-companion-modules)
  - [7.1 Integer modules](Stdlib-Design#71-integer-modules)
  - [7.2 Arithmetic without wrapping intrinsics](Stdlib-Design#72-arithmetic-without-wrapping-intrinsics)
  - [7.3 Float modules](Stdlib-Design#73-float-modules)
  - [7.4 `Bool`](Stdlib-Design#74-bool)
  - [7.5 Float text conversion](Stdlib-Design#75-float-text-conversion)
- [8. `Std.Text`](Stdlib-Design#8-stdtext)
  - [8.1 `Strings`](Stdlib-Design#81-strings)
  - [8.2 `StringBuilder`](Stdlib-Design#82-stringbuilder)
  - [8.3 `Scalars`](Stdlib-Design#83-scalars)
  - [8.4 `Regex`](Stdlib-Design#84-regex)
- [9. `Std.Collections`](Stdlib-Design#9-stdcollections)
  - [9.1 Contracts](Stdlib-Design#91-contracts)
  - [9.2 Equality of collections](Stdlib-Design#92-equality-of-collections)
  - [9.3 `ArrayList<T>`](Stdlib-Design#93-arraylistt)
  - [9.4 `ArrayDeque<T>`](Stdlib-Design#94-arraydequet)
  - [9.5 `LinkedList<T>`](Stdlib-Design#95-linkedlistt)
  - [9.6 Hash collections](Stdlib-Design#96-hash-collections)
  - [9.7 `TreeMap<K, V>` and `TreeSet<T>`](Stdlib-Design#97-treemapk-v-and-treesett)
  - [9.8 `PriorityQueue<T>`](Stdlib-Design#98-priorityqueuet)
  - [9.9 `BitSet` and `OrdinalMap<V>`](Stdlib-Design#99-bitset-and-ordinalmapv)
  - [9.10 Interfaces and read-only views](Stdlib-Design#910-interfaces-and-read-only-views)
  - [9.11 Sorting and searching](Stdlib-Design#911-sorting-and-searching)
  - [9.12 `Sequence<T>`](Stdlib-Design#912-sequencet)
- [10. `Std.Math`](Stdlib-Design#10-stdmath)
  - [10.1 `Math`](Stdlib-Design#101-math)
  - [10.2 `BigInteger`](Stdlib-Design#102-biginteger)
  - [10.3 `BigDecimal`](Stdlib-Design#103-bigdecimal)
- [11. Input and output](Stdlib-Design#11-input-and-output)
  - [11.1 `Std.Io`](Stdlib-Design#111-stdio)
  - [11.2 `Std.Fs`](Stdlib-Design#112-stdfs)
  - [11.3 `Std.Program`](Stdlib-Design#113-stdprogram)
  - [11.4 `Std.Net`](Stdlib-Design#114-stdnet)
- [12. Time and randomness](Stdlib-Design#12-time-and-randomness)
  - [12.1 `Std.Time`](Stdlib-Design#121-stdtime)
  - [12.2 `Std.Random`](Stdlib-Design#122-stdrandom)
- [13. Data formats](Stdlib-Design#13-data-formats)
  - [13.1 `Std.Encoding`](Stdlib-Design#131-stdencoding)
  - [13.2 `Std.Digest`](Stdlib-Design#132-stddigest)
- [14. Diagnostics](Stdlib-Design#14-diagnostics)
  - [14.1 `Std.Log`](Stdlib-Design#141-stdlog)
  - [14.2 `Std.Test`](Stdlib-Design#142-stdtest)
- [15. Names](Stdlib-Design#15-names)
- [16. Not provided](Stdlib-Design#16-not-provided)
- [17. Verification](Stdlib-Design#17-verification)
- [18. Behavior details](Stdlib-Design#18-behavior-details)
  - [18.1 Language and layout](Stdlib-Design#181-language-and-layout)
  - [18.2 Program, console, and testing](Stdlib-Design#182-program-console-and-testing)
  - [18.3 Numbers and text building](Stdlib-Design#183-numbers-and-text-building)
  - [18.4 Text](Stdlib-Design#184-text)
  - [18.5 Time, randomness, and logging](Stdlib-Design#185-time-randomness-and-logging)
  - [18.6 Host services, files, and networking](Stdlib-Design#186-host-services-files-and-networking)
  - [18.7 Encodings and digests](Stdlib-Design#187-encodings-and-digests)
  - [18.8 Lists, deques, queues, and sorting](Stdlib-Design#188-lists-deques-queues-and-sorting)
  - [18.9 Hash collections, trees, and sequences](Stdlib-Design#189-hash-collections-trees-and-sequences)
  - [18.10 Math](Stdlib-Design#1810-math)
  - [18.11 Test layout](Stdlib-Design#1811-test-layout)
  - [18.12 Concurrency](Stdlib-Design#1812-concurrency)
- [19. `Std.Concurrent`](Stdlib-Design#19-stdconcurrent)

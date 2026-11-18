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
| [`Std.Core.Arithmetic`](Stdlib-Core-Arithmetic) | [Contracts](Stdlib-Core-Arithmetic#detailed-contracts) | [Declarations](Stdlib-Core-Arithmetic#exported-declarations) |
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

The networking contracts, TCP streams and listeners, and the packages added in 0.3.0 (proposed; some host calls are stubs). The [specification](Stdlib-Design#114-stdnet) describes each package, and the design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository lists what each provides and what each stub needs.

| Module | Detailed contracts | Exported declarations |
| --- | --- | --- |
| [`Std.Net.Connector`](Stdlib-Net-Connector) | [Contracts](Stdlib-Net-Connector#detailed-contracts) | [Declarations](Stdlib-Net-Connector#exported-declarations) |
| [`Std.Net.Core.ByteRing`](Stdlib-Net-Core-ByteRing) | [Contracts](Stdlib-Net-Core-ByteRing#detailed-contracts) | [Declarations](Stdlib-Net-Core-ByteRing#exported-declarations) |
| [`Std.Net.Core.CidrBlock`](Stdlib-Net-Core-CidrBlock) | [Contracts](Stdlib-Net-Core-CidrBlock#detailed-contracts) | [Declarations](Stdlib-Net-Core-CidrBlock#exported-declarations) |
| [`Std.Net.Core.Idna`](Stdlib-Net-Core-Idna) | [Contracts](Stdlib-Net-Core-Idna#detailed-contracts) | [Declarations](Stdlib-Net-Core-Idna#exported-declarations) |
| [`Std.Net.Core.IoSlice`](Stdlib-Net-Core-IoSlice) | [Contracts](Stdlib-Net-Core-IoSlice#detailed-contracts) | [Declarations](Stdlib-Net-Core-IoSlice#exported-declarations) |
| [`Std.Net.Core.IpAddr`](Stdlib-Net-Core-IpAddr) | [Contracts](Stdlib-Net-Core-IpAddr#detailed-contracts) | [Declarations](Stdlib-Net-Core-IpAddr#exported-declarations) |
| [`Std.Net.Core.Ipv4Addr`](Stdlib-Net-Core-Ipv4Addr) | [Contracts](Stdlib-Net-Core-Ipv4Addr#detailed-contracts) | [Declarations](Stdlib-Net-Core-Ipv4Addr#exported-declarations) |
| [`Std.Net.Core.Ipv6Addr`](Stdlib-Net-Core-Ipv6Addr) | [Contracts](Stdlib-Net-Core-Ipv6Addr#detailed-contracts) | [Declarations](Stdlib-Net-Core-Ipv6Addr#exported-declarations) |
| [`Std.Net.Core.NetAddress`](Stdlib-Net-Core-NetAddress) | [Contracts](Stdlib-Net-Core-NetAddress#detailed-contracts) | [Declarations](Stdlib-Net-Core-NetAddress#exported-declarations) |
| [`Std.Net.Core.PercentEncoding`](Stdlib-Net-Core-PercentEncoding) | [Contracts](Stdlib-Net-Core-PercentEncoding#detailed-contracts) | [Declarations](Stdlib-Net-Core-PercentEncoding#exported-declarations) |
| [`Std.Net.Core.PrefixTrie`](Stdlib-Net-Core-PrefixTrie) | [Contracts](Stdlib-Net-Core-PrefixTrie#detailed-contracts) | [Declarations](Stdlib-Net-Core-PrefixTrie#exported-declarations) |
| [`Std.Net.Core.Punycode`](Stdlib-Net-Core-Punycode) | [Contracts](Stdlib-Net-Core-Punycode#detailed-contracts) | [Declarations](Stdlib-Net-Core-Punycode#exported-declarations) |
| [`Std.Net.Core.Query`](Stdlib-Net-Core-Query) | [Contracts](Stdlib-Net-Core-Query#detailed-contracts) | [Declarations](Stdlib-Net-Core-Query#exported-declarations) |
| [`Std.Net.Core.SocketAddr`](Stdlib-Net-Core-SocketAddr) | [Contracts](Stdlib-Net-Core-SocketAddr#detailed-contracts) | [Declarations](Stdlib-Net-Core-SocketAddr#exported-declarations) |
| [`Std.Net.Core.UnixAddr`](Stdlib-Net-Core-UnixAddr) | [Contracts](Stdlib-Net-Core-UnixAddr#detailed-contracts) | [Declarations](Stdlib-Net-Core-UnixAddr#exported-declarations) |
| [`Std.Net.Core.Uri`](Stdlib-Net-Core-Uri) | [Contracts](Stdlib-Net-Core-Uri#detailed-contracts) | [Declarations](Stdlib-Net-Core-Uri#exported-declarations) |
| [`Std.Net.Core.Urn`](Stdlib-Net-Core-Urn) | [Contracts](Stdlib-Net-Core-Urn#detailed-contracts) | [Declarations](Stdlib-Net-Core-Urn#exported-declarations) |
| [`Std.Net.Dns.DnsCache`](Stdlib-Net-Dns-DnsCache) | [Contracts](Stdlib-Net-Dns-DnsCache#detailed-contracts) | [Declarations](Stdlib-Net-Dns-DnsCache#exported-declarations) |
| [`Std.Net.Dns.DnsTransport`](Stdlib-Net-Dns-DnsTransport) | [Contracts](Stdlib-Net-Dns-DnsTransport#detailed-contracts) | [Declarations](Stdlib-Net-Dns-DnsTransport#exported-declarations) |
| [`Std.Net.Dns.DohTransport`](Stdlib-Net-Dns-DohTransport) | [Contracts](Stdlib-Net-Dns-DohTransport#detailed-contracts) | [Declarations](Stdlib-Net-Dns-DohTransport#exported-declarations) |
| [`Std.Net.Dns.DoqTransport`](Stdlib-Net-Dns-DoqTransport) | [Contracts](Stdlib-Net-Dns-DoqTransport#detailed-contracts) | [Declarations](Stdlib-Net-Dns-DoqTransport#exported-declarations) |
| [`Std.Net.Dns.DotTransport`](Stdlib-Net-Dns-DotTransport) | [Contracts](Stdlib-Net-Dns-DotTransport#detailed-contracts) | [Declarations](Stdlib-Net-Dns-DotTransport#exported-declarations) |
| [`Std.Net.Dns.HostsFile`](Stdlib-Net-Dns-HostsFile) | [Contracts](Stdlib-Net-Dns-HostsFile#detailed-contracts) | [Declarations](Stdlib-Net-Dns-HostsFile#exported-declarations) |
| [`Std.Net.Dns.Mdns`](Stdlib-Net-Dns-Mdns) | [Contracts](Stdlib-Net-Dns-Mdns#detailed-contracts) | [Declarations](Stdlib-Net-Dns-Mdns#exported-declarations) |
| [`Std.Net.Dns.Message`](Stdlib-Net-Dns-Message) | [Contracts](Stdlib-Net-Dns-Message#detailed-contracts) | [Declarations](Stdlib-Net-Dns-Message#exported-declarations) |
| [`Std.Net.Dns.Name`](Stdlib-Net-Dns-Name) | [Contracts](Stdlib-Net-Dns-Name#detailed-contracts) | [Declarations](Stdlib-Net-Dns-Name#exported-declarations) |
| [`Std.Net.Dns.NaptrLookup`](Stdlib-Net-Dns-NaptrLookup) | [Contracts](Stdlib-Net-Dns-NaptrLookup#detailed-contracts) | [Declarations](Stdlib-Net-Dns-NaptrLookup#exported-declarations) |
| [`Std.Net.Dns.ResolvConf`](Stdlib-Net-Dns-ResolvConf) | [Contracts](Stdlib-Net-Dns-ResolvConf#detailed-contracts) | [Declarations](Stdlib-Net-Dns-ResolvConf#exported-declarations) |
| [`Std.Net.Dns.SrvLookup`](Stdlib-Net-Dns-SrvLookup) | [Contracts](Stdlib-Net-Dns-SrvLookup#detailed-contracts) | [Declarations](Stdlib-Net-Dns-SrvLookup#exported-declarations) |
| [`Std.Net.Dns.StubResolver`](Stdlib-Net-Dns-StubResolver) | [Contracts](Stdlib-Net-Dns-StubResolver#detailed-contracts) | [Declarations](Stdlib-Net-Dns-StubResolver#exported-declarations) |
| [`Std.Net.Dns.SystemResolver`](Stdlib-Net-Dns-SystemResolver) | [Contracts](Stdlib-Net-Dns-SystemResolver#detailed-contracts) | [Declarations](Stdlib-Net-Dns-SystemResolver#exported-declarations) |
| [`Std.Net.Dns.TcpDnsTransport`](Stdlib-Net-Dns-TcpDnsTransport) | [Contracts](Stdlib-Net-Dns-TcpDnsTransport#detailed-contracts) | [Declarations](Stdlib-Net-Dns-TcpDnsTransport#exported-declarations) |
| [`Std.Net.Dns.UdpDnsTransport`](Stdlib-Net-Dns-UdpDnsTransport) | [Contracts](Stdlib-Net-Dns-UdpDnsTransport#detailed-contracts) | [Declarations](Stdlib-Net-Dns-UdpDnsTransport#exported-declarations) |
| [`Std.Net.Dns.Wire`](Stdlib-Net-Dns-Wire) | [Contracts](Stdlib-Net-Dns-Wire#detailed-contracts) | [Declarations](Stdlib-Net-Dns-Wire#exported-declarations) |
| [`Std.Net.Endpoint`](Stdlib-Net-Endpoint) | [Contracts](Stdlib-Net-Endpoint#detailed-contracts) | [Declarations](Stdlib-Net-Endpoint#exported-declarations) |
| [`Std.Net.Filter`](Stdlib-Net-Filter) | [Contracts](Stdlib-Net-Filter#detailed-contracts) | [Declarations](Stdlib-Net-Filter#exported-declarations) |
| [`Std.Net.FramedTransport`](Stdlib-Net-FramedTransport) | [Contracts](Stdlib-Net-FramedTransport#detailed-contracts) | [Declarations](Stdlib-Net-FramedTransport#exported-declarations) |
| [`Std.Net.Http`](Stdlib-Net-Http) | [Contracts](Stdlib-Net-Http#detailed-contracts) | [Declarations](Stdlib-Net-Http#exported-declarations) |
| [`Std.Net.Http.Alpn`](Stdlib-Net-Http-Alpn) | [Contracts](Stdlib-Net-Http-Alpn#detailed-contracts) | [Declarations](Stdlib-Net-Http-Alpn#exported-declarations) |
| [`Std.Net.Http.AltSvc`](Stdlib-Net-Http-AltSvc) | [Contracts](Stdlib-Net-Http-AltSvc#detailed-contracts) | [Declarations](Stdlib-Net-Http-AltSvc#exported-declarations) |
| [`Std.Net.Http.AppHandler`](Stdlib-Net-Http-AppHandler) | [Contracts](Stdlib-Net-Http-AppHandler#detailed-contracts) | [Declarations](Stdlib-Net-Http-AppHandler#exported-declarations) |
| [`Std.Net.Http.Cache`](Stdlib-Net-Http-Cache) | [Contracts](Stdlib-Net-Http-Cache#detailed-contracts) | [Declarations](Stdlib-Net-Http-Cache#exported-declarations) |
| [`Std.Net.Http.CacheControl`](Stdlib-Net-Http-CacheControl) | [Contracts](Stdlib-Net-Http-CacheControl#detailed-contracts) | [Declarations](Stdlib-Net-Http-CacheControl#exported-declarations) |
| [`Std.Net.Http.Chunked`](Stdlib-Net-Http-Chunked) | [Contracts](Stdlib-Net-Http-Chunked#detailed-contracts) | [Declarations](Stdlib-Net-Http-Chunked#exported-declarations) |
| [`Std.Net.Http.Date`](Stdlib-Net-Http-Date) | [Contracts](Stdlib-Net-Http-Date#detailed-contracts) | [Declarations](Stdlib-Net-Http-Date#exported-declarations) |
| [`Std.Net.Http.H2`](Stdlib-Net-Http-H2) | [Contracts](Stdlib-Net-Http-H2#detailed-contracts) | [Declarations](Stdlib-Net-Http-H2#exported-declarations) |
| [`Std.Net.Http.H3`](Stdlib-Net-Http-H3) | [Contracts](Stdlib-Net-Http-H3#detailed-contracts) | [Declarations](Stdlib-Net-Http-H3#exported-declarations) |
| [`Std.Net.Http.Headers`](Stdlib-Net-Http-Headers) | [Contracts](Stdlib-Net-Http-Headers#detailed-contracts) | [Declarations](Stdlib-Net-Http-Headers#exported-declarations) |
| [`Std.Net.Http.Masque`](Stdlib-Net-Http-Masque) | [Contracts](Stdlib-Net-Http-Masque#detailed-contracts) | [Declarations](Stdlib-Net-Http-Masque#exported-declarations) |
| [`Std.Net.Http.Message`](Stdlib-Net-Http-Message) | [Contracts](Stdlib-Net-Http-Message#detailed-contracts) | [Declarations](Stdlib-Net-Http-Message#exported-declarations) |
| [`Std.Net.Http.Method`](Stdlib-Net-Http-Method) | [Contracts](Stdlib-Net-Http-Method#detailed-contracts) | [Declarations](Stdlib-Net-Http-Method#exported-declarations) |
| [`Std.Net.Http.Multipart`](Stdlib-Net-Http-Multipart) | [Contracts](Stdlib-Net-Http-Multipart#detailed-contracts) | [Declarations](Stdlib-Net-Http-Multipart#exported-declarations) |
| [`Std.Net.Http.Pool`](Stdlib-Net-Http-Pool) | [Contracts](Stdlib-Net-Http-Pool#detailed-contracts) | [Declarations](Stdlib-Net-Http-Pool#exported-declarations) |
| [`Std.Net.Http.Status`](Stdlib-Net-Http-Status) | [Contracts](Stdlib-Net-Http-Status#detailed-contracts) | [Declarations](Stdlib-Net-Http-Status#exported-declarations) |
| [`Std.Net.Http.Wire`](Stdlib-Net-Http-Wire) | [Contracts](Stdlib-Net-Http-Wire#detailed-contracts) | [Declarations](Stdlib-Net-Http-Wire#exported-declarations) |
| [`Std.Net.Listener`](Stdlib-Net-Listener) | [Contracts](Stdlib-Net-Listener#detailed-contracts) | [Declarations](Stdlib-Net-Listener#exported-declarations) |
| [`Std.Net.Mesh.Baggage`](Stdlib-Net-Mesh-Baggage) | [Contracts](Stdlib-Net-Mesh-Baggage#detailed-contracts) | [Declarations](Stdlib-Net-Mesh-Baggage#exported-declarations) |
| [`Std.Net.Mesh.Crc32c`](Stdlib-Net-Mesh-Crc32c) | [Contracts](Stdlib-Net-Mesh-Crc32c#detailed-contracts) | [Declarations](Stdlib-Net-Mesh-Crc32c#exported-declarations) |
| [`Std.Net.Mesh.Ebpf`](Stdlib-Net-Mesh-Ebpf) | [Contracts](Stdlib-Net-Mesh-Ebpf#detailed-contracts) | [Declarations](Stdlib-Net-Mesh-Ebpf#exported-declarations) |
| [`Std.Net.Mesh.ProxyProtocol`](Stdlib-Net-Mesh-ProxyProtocol) | [Contracts](Stdlib-Net-Mesh-ProxyProtocol#detailed-contracts) | [Declarations](Stdlib-Net-Mesh-ProxyProtocol#exported-declarations) |
| [`Std.Net.Mesh.ProxyProtocolListener`](Stdlib-Net-Mesh-ProxyProtocolListener) | [Contracts](Stdlib-Net-Mesh-ProxyProtocolListener#detailed-contracts) | [Declarations](Stdlib-Net-Mesh-ProxyProtocolListener#exported-declarations) |
| [`Std.Net.Mesh.ShadowFilter`](Stdlib-Net-Mesh-ShadowFilter) | [Contracts](Stdlib-Net-Mesh-ShadowFilter#detailed-contracts) | [Declarations](Stdlib-Net-Mesh-ShadowFilter#exported-declarations) |
| [`Std.Net.Mesh.TraceContext`](Stdlib-Net-Mesh-TraceContext) | [Contracts](Stdlib-Net-Mesh-TraceContext#detailed-contracts) | [Declarations](Stdlib-Net-Mesh-TraceContext#exported-declarations) |
| [`Std.Net.Mesh.TraceParent`](Stdlib-Net-Mesh-TraceParent) | [Contracts](Stdlib-Net-Mesh-TraceParent#detailed-contracts) | [Declarations](Stdlib-Net-Mesh-TraceParent#exported-declarations) |
| [`Std.Net.Mesh.TracePropagationFilter`](Stdlib-Net-Mesh-TracePropagationFilter) | [Contracts](Stdlib-Net-Mesh-TracePropagationFilter#detailed-contracts) | [Declarations](Stdlib-Net-Mesh-TracePropagationFilter#exported-declarations) |
| [`Std.Net.Mesh.TraceState`](Stdlib-Net-Mesh-TraceState) | [Contracts](Stdlib-Net-Mesh-TraceState#detailed-contracts) | [Declarations](Stdlib-Net-Mesh-TraceState#exported-declarations) |
| [`Std.Net.MultiplexedTransport`](Stdlib-Net-MultiplexedTransport) | [Contracts](Stdlib-Net-MultiplexedTransport#detailed-contracts) | [Declarations](Stdlib-Net-MultiplexedTransport#exported-declarations) |
| [`Std.Net.Quic.Bbr`](Stdlib-Net-Quic-Bbr) | [Contracts](Stdlib-Net-Quic-Bbr#detailed-contracts) | [Declarations](Stdlib-Net-Quic-Bbr#exported-declarations) |
| [`Std.Net.Quic.CongestionController`](Stdlib-Net-Quic-CongestionController) | [Contracts](Stdlib-Net-Quic-CongestionController#detailed-contracts) | [Declarations](Stdlib-Net-Quic-CongestionController#exported-declarations) |
| [`Std.Net.Quic.Cubic`](Stdlib-Net-Quic-Cubic) | [Contracts](Stdlib-Net-Quic-Cubic#detailed-contracts) | [Declarations](Stdlib-Net-Quic-Cubic#exported-declarations) |
| [`Std.Net.Quic.DatagramFrame`](Stdlib-Net-Quic-DatagramFrame) | [Contracts](Stdlib-Net-Quic-DatagramFrame#detailed-contracts) | [Declarations](Stdlib-Net-Quic-DatagramFrame#exported-declarations) |
| [`Std.Net.Quic.Ledbat`](Stdlib-Net-Quic-Ledbat) | [Contracts](Stdlib-Net-Quic-Ledbat#detailed-contracts) | [Declarations](Stdlib-Net-Quic-Ledbat#exported-declarations) |
| [`Std.Net.Quic.NewReno`](Stdlib-Net-Quic-NewReno) | [Contracts](Stdlib-Net-Quic-NewReno#detailed-contracts) | [Declarations](Stdlib-Net-Quic-NewReno#exported-declarations) |
| [`Std.Net.Quic.QuicConfig`](Stdlib-Net-Quic-QuicConfig) | [Contracts](Stdlib-Net-Quic-QuicConfig#detailed-contracts) | [Declarations](Stdlib-Net-Quic-QuicConfig#exported-declarations) |
| [`Std.Net.Quic.QuicConnection`](Stdlib-Net-Quic-QuicConnection) | [Contracts](Stdlib-Net-Quic-QuicConnection#detailed-contracts) | [Declarations](Stdlib-Net-Quic-QuicConnection#exported-declarations) |
| [`Std.Net.Quic.QuicEndpoint`](Stdlib-Net-Quic-QuicEndpoint) | [Contracts](Stdlib-Net-Quic-QuicEndpoint#detailed-contracts) | [Declarations](Stdlib-Net-Quic-QuicEndpoint#exported-declarations) |
| [`Std.Net.Quic.ReplayFilter`](Stdlib-Net-Quic-ReplayFilter) | [Contracts](Stdlib-Net-Quic-ReplayFilter#detailed-contracts) | [Declarations](Stdlib-Net-Quic-ReplayFilter#exported-declarations) |
| [`Std.Net.Quic.SessionTicket`](Stdlib-Net-Quic-SessionTicket) | [Contracts](Stdlib-Net-Quic-SessionTicket#detailed-contracts) | [Declarations](Stdlib-Net-Quic-SessionTicket#exported-declarations) |
| [`Std.Net.Quic.StreamId`](Stdlib-Net-Quic-StreamId) | [Contracts](Stdlib-Net-Quic-StreamId#detailed-contracts) | [Declarations](Stdlib-Net-Quic-StreamId#exported-declarations) |
| [`Std.Net.Quic.VarInt`](Stdlib-Net-Quic-VarInt) | [Contracts](Stdlib-Net-Quic-VarInt#detailed-contracts) | [Declarations](Stdlib-Net-Quic-VarInt#exported-declarations) |
| [`Std.Net.Realtime.Sse.Client`](Stdlib-Net-Realtime-Sse-Client) | [Contracts](Stdlib-Net-Realtime-Sse-Client#detailed-contracts) | [Declarations](Stdlib-Net-Realtime-Sse-Client#exported-declarations) |
| [`Std.Net.Realtime.Sse.Emitter`](Stdlib-Net-Realtime-Sse-Emitter) | [Contracts](Stdlib-Net-Realtime-Sse-Emitter#detailed-contracts) | [Declarations](Stdlib-Net-Realtime-Sse-Emitter#exported-declarations) |
| [`Std.Net.Realtime.Sse.Parser`](Stdlib-Net-Realtime-Sse-Parser) | [Contracts](Stdlib-Net-Realtime-Sse-Parser#detailed-contracts) | [Declarations](Stdlib-Net-Realtime-Sse-Parser#exported-declarations) |
| [`Std.Net.Realtime.Sse.SseEvent`](Stdlib-Net-Realtime-Sse-SseEvent) | [Contracts](Stdlib-Net-Realtime-Sse-SseEvent#detailed-contracts) | [Declarations](Stdlib-Net-Realtime-Sse-SseEvent#exported-declarations) |
| [`Std.Net.Realtime.WebTransport.Session`](Stdlib-Net-Realtime-WebTransport-Session) | [Contracts](Stdlib-Net-Realtime-WebTransport-Session#detailed-contracts) | [Declarations](Stdlib-Net-Realtime-WebTransport-Session#exported-declarations) |
| [`Std.Net.Realtime.WebTransport.Wire`](Stdlib-Net-Realtime-WebTransport-Wire) | [Contracts](Stdlib-Net-Realtime-WebTransport-Wire#detailed-contracts) | [Declarations](Stdlib-Net-Realtime-WebTransport-Wire#exported-declarations) |
| [`Std.Net.Realtime.Ws.CloseStatus`](Stdlib-Net-Realtime-Ws-CloseStatus) | [Contracts](Stdlib-Net-Realtime-Ws-CloseStatus#detailed-contracts) | [Declarations](Stdlib-Net-Realtime-Ws-CloseStatus#exported-declarations) |
| [`Std.Net.Realtime.Ws.ExtendedConnect`](Stdlib-Net-Realtime-Ws-ExtendedConnect) | [Contracts](Stdlib-Net-Realtime-Ws-ExtendedConnect#detailed-contracts) | [Declarations](Stdlib-Net-Realtime-Ws-ExtendedConnect#exported-declarations) |
| [`Std.Net.Realtime.Ws.Extensions`](Stdlib-Net-Realtime-Ws-Extensions) | [Contracts](Stdlib-Net-Realtime-Ws-Extensions#detailed-contracts) | [Declarations](Stdlib-Net-Realtime-Ws-Extensions#exported-declarations) |
| [`Std.Net.Realtime.Ws.Frame`](Stdlib-Net-Realtime-Ws-Frame) | [Contracts](Stdlib-Net-Realtime-Ws-Frame#detailed-contracts) | [Declarations](Stdlib-Net-Realtime-Ws-Frame#exported-declarations) |
| [`Std.Net.Realtime.Ws.Handshake`](Stdlib-Net-Realtime-Ws-Handshake) | [Contracts](Stdlib-Net-Realtime-Ws-Handshake#detailed-contracts) | [Declarations](Stdlib-Net-Realtime-Ws-Handshake#exported-declarations) |
| [`Std.Net.Realtime.Ws.Heartbeat`](Stdlib-Net-Realtime-Ws-Heartbeat) | [Contracts](Stdlib-Net-Realtime-Ws-Heartbeat#detailed-contracts) | [Declarations](Stdlib-Net-Realtime-Ws-Heartbeat#exported-declarations) |
| [`Std.Net.Realtime.Ws.Message`](Stdlib-Net-Realtime-Ws-Message) | [Contracts](Stdlib-Net-Realtime-Ws-Message#detailed-contracts) | [Declarations](Stdlib-Net-Realtime-Ws-Message#exported-declarations) |
| [`Std.Net.Realtime.Ws.PermessageDeflate`](Stdlib-Net-Realtime-Ws-PermessageDeflate) | [Contracts](Stdlib-Net-Realtime-Ws-PermessageDeflate#detailed-contracts) | [Declarations](Stdlib-Net-Realtime-Ws-PermessageDeflate#exported-declarations) |
| [`Std.Net.Realtime.Ws.WebSocket`](Stdlib-Net-Realtime-Ws-WebSocket) | [Contracts](Stdlib-Net-Realtime-Ws-WebSocket#detailed-contracts) | [Declarations](Stdlib-Net-Realtime-Ws-WebSocket#exported-declarations) |
| [`Std.Net.Resilience.AdaptiveLimit`](Stdlib-Net-Resilience-AdaptiveLimit) | [Contracts](Stdlib-Net-Resilience-AdaptiveLimit#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-AdaptiveLimit#exported-declarations) |
| [`Std.Net.Resilience.Backoff`](Stdlib-Net-Resilience-Backoff) | [Contracts](Stdlib-Net-Resilience-Backoff#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-Backoff#exported-declarations) |
| [`Std.Net.Resilience.CircuitBreaker`](Stdlib-Net-Resilience-CircuitBreaker) | [Contracts](Stdlib-Net-Resilience-CircuitBreaker#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-CircuitBreaker#exported-declarations) |
| [`Std.Net.Resilience.CircuitBreakerFilter`](Stdlib-Net-Resilience-CircuitBreakerFilter) | [Contracts](Stdlib-Net-Resilience-CircuitBreakerFilter#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-CircuitBreakerFilter#exported-declarations) |
| [`Std.Net.Resilience.ConcurrencyLimitFilter`](Stdlib-Net-Resilience-ConcurrencyLimitFilter) | [Contracts](Stdlib-Net-Resilience-ConcurrencyLimitFilter#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-ConcurrencyLimitFilter#exported-declarations) |
| [`Std.Net.Resilience.GcraLimiter`](Stdlib-Net-Resilience-GcraLimiter) | [Contracts](Stdlib-Net-Resilience-GcraLimiter#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-GcraLimiter#exported-declarations) |
| [`Std.Net.Resilience.GradientLimit`](Stdlib-Net-Resilience-GradientLimit) | [Contracts](Stdlib-Net-Resilience-GradientLimit#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-GradientLimit#exported-declarations) |
| [`Std.Net.Resilience.HedgingFilter`](Stdlib-Net-Resilience-HedgingFilter) | [Contracts](Stdlib-Net-Resilience-HedgingFilter#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-HedgingFilter#exported-declarations) |
| [`Std.Net.Resilience.InMemoryRateStore`](Stdlib-Net-Resilience-InMemoryRateStore) | [Contracts](Stdlib-Net-Resilience-InMemoryRateStore#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-InMemoryRateStore#exported-declarations) |
| [`Std.Net.Resilience.LatencyWindow`](Stdlib-Net-Resilience-LatencyWindow) | [Contracts](Stdlib-Net-Resilience-LatencyWindow#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-LatencyWindow#exported-declarations) |
| [`Std.Net.Resilience.LoadBalancer`](Stdlib-Net-Resilience-LoadBalancer) | [Contracts](Stdlib-Net-Resilience-LoadBalancer#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-LoadBalancer#exported-declarations) |
| [`Std.Net.Resilience.PeakEwma`](Stdlib-Net-Resilience-PeakEwma) | [Contracts](Stdlib-Net-Resilience-PeakEwma#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-PeakEwma#exported-declarations) |
| [`Std.Net.Resilience.PowerOfTwoChoices`](Stdlib-Net-Resilience-PowerOfTwoChoices) | [Contracts](Stdlib-Net-Resilience-PowerOfTwoChoices#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-PowerOfTwoChoices#exported-declarations) |
| [`Std.Net.Resilience.RateLimitFilter`](Stdlib-Net-Resilience-RateLimitFilter) | [Contracts](Stdlib-Net-Resilience-RateLimitFilter#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-RateLimitFilter#exported-declarations) |
| [`Std.Net.Resilience.RateLimiter`](Stdlib-Net-Resilience-RateLimiter) | [Contracts](Stdlib-Net-Resilience-RateLimiter#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-RateLimiter#exported-declarations) |
| [`Std.Net.Resilience.RateStore`](Stdlib-Net-Resilience-RateStore) | [Contracts](Stdlib-Net-Resilience-RateStore#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-RateStore#exported-declarations) |
| [`Std.Net.Resilience.RetryBudget`](Stdlib-Net-Resilience-RetryBudget) | [Contracts](Stdlib-Net-Resilience-RetryBudget#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-RetryBudget#exported-declarations) |
| [`Std.Net.Resilience.RetryFilter`](Stdlib-Net-Resilience-RetryFilter) | [Contracts](Stdlib-Net-Resilience-RetryFilter#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-RetryFilter#exported-declarations) |
| [`Std.Net.Resilience.RoundRobin`](Stdlib-Net-Resilience-RoundRobin) | [Contracts](Stdlib-Net-Resilience-RoundRobin#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-RoundRobin#exported-declarations) |
| [`Std.Net.Resilience.SpawnableService`](Stdlib-Net-Resilience-SpawnableService) | [Contracts](Stdlib-Net-Resilience-SpawnableService#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-SpawnableService#exported-declarations) |
| [`Std.Net.Resilience.TokenBucket`](Stdlib-Net-Resilience-TokenBucket) | [Contracts](Stdlib-Net-Resilience-TokenBucket#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-TokenBucket#exported-declarations) |
| [`Std.Net.Resilience.TokenBucketLimiter`](Stdlib-Net-Resilience-TokenBucketLimiter) | [Contracts](Stdlib-Net-Resilience-TokenBucketLimiter#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-TokenBucketLimiter#exported-declarations) |
| [`Std.Net.Resilience.VegasLimit`](Stdlib-Net-Resilience-VegasLimit) | [Contracts](Stdlib-Net-Resilience-VegasLimit#detailed-contracts) | [Declarations](Stdlib-Net-Resilience-VegasLimit#exported-declarations) |
| [`Std.Net.Rpc.Call`](Stdlib-Net-Rpc-Call) | [Contracts](Stdlib-Net-Rpc-Call#detailed-contracts) | [Declarations](Stdlib-Net-Rpc-Call#exported-declarations) |
| [`Std.Net.Rpc.Codec`](Stdlib-Net-Rpc-Codec) | [Contracts](Stdlib-Net-Rpc-Codec#detailed-contracts) | [Declarations](Stdlib-Net-Rpc-Codec#exported-declarations) |
| [`Std.Net.Rpc.Deadline`](Stdlib-Net-Rpc-Deadline) | [Contracts](Stdlib-Net-Rpc-Deadline#detailed-contracts) | [Declarations](Stdlib-Net-Rpc-Deadline#exported-declarations) |
| [`Std.Net.Rpc.Framing`](Stdlib-Net-Rpc-Framing) | [Contracts](Stdlib-Net-Rpc-Framing#detailed-contracts) | [Declarations](Stdlib-Net-Rpc-Framing#exported-declarations) |
| [`Std.Net.Rpc.Grpc`](Stdlib-Net-Rpc-Grpc) | [Contracts](Stdlib-Net-Rpc-Grpc#detailed-contracts) | [Declarations](Stdlib-Net-Rpc-Grpc#exported-declarations) |
| [`Std.Net.Rpc.GrpcWeb`](Stdlib-Net-Rpc-GrpcWeb) | [Contracts](Stdlib-Net-Rpc-GrpcWeb#detailed-contracts) | [Declarations](Stdlib-Net-Rpc-GrpcWeb#exported-declarations) |
| [`Std.Net.Rpc.Health`](Stdlib-Net-Rpc-Health) | [Contracts](Stdlib-Net-Rpc-Health#detailed-contracts) | [Declarations](Stdlib-Net-Rpc-Health#exported-declarations) |
| [`Std.Net.Rpc.Metadata`](Stdlib-Net-Rpc-Metadata) | [Contracts](Stdlib-Net-Rpc-Metadata#detailed-contracts) | [Declarations](Stdlib-Net-Rpc-Metadata#exported-declarations) |
| [`Std.Net.Rpc.ProtoWire`](Stdlib-Net-Rpc-ProtoWire) | [Contracts](Stdlib-Net-Rpc-ProtoWire#detailed-contracts) | [Declarations](Stdlib-Net-Rpc-ProtoWire#exported-declarations) |
| [`Std.Net.Rpc.Reflection`](Stdlib-Net-Rpc-Reflection) | [Contracts](Stdlib-Net-Rpc-Reflection#detailed-contracts) | [Declarations](Stdlib-Net-Rpc-Reflection#exported-declarations) |
| [`Std.Net.Rpc.Status`](Stdlib-Net-Rpc-Status) | [Contracts](Stdlib-Net-Rpc-Status#detailed-contracts) | [Declarations](Stdlib-Net-Rpc-Status#exported-declarations) |
| [`Std.Net.Rpc.Timeout`](Stdlib-Net-Rpc-Timeout) | [Contracts](Stdlib-Net-Rpc-Timeout#detailed-contracts) | [Declarations](Stdlib-Net-Rpc-Timeout#exported-declarations) |
| [`Std.Net.Service`](Stdlib-Net-Service) | [Contracts](Stdlib-Net-Service#detailed-contracts) | [Declarations](Stdlib-Net-Service#exported-declarations) |
| [`Std.Net.Socket.DatagramSocket`](Stdlib-Net-Socket-DatagramSocket) | [Contracts](Stdlib-Net-Socket-DatagramSocket#detailed-contracts) | [Declarations](Stdlib-Net-Socket-DatagramSocket#exported-declarations) |
| [`Std.Net.Socket.EventLoop`](Stdlib-Net-Socket-EventLoop) | [Contracts](Stdlib-Net-Socket-EventLoop#detailed-contracts) | [Declarations](Stdlib-Net-Socket-EventLoop#exported-declarations) |
| [`Std.Net.Socket.Offload`](Stdlib-Net-Socket-Offload) | [Contracts](Stdlib-Net-Socket-Offload#detailed-contracts) | [Declarations](Stdlib-Net-Socket-Offload#exported-declarations) |
| [`Std.Net.Socket.RawSocket`](Stdlib-Net-Socket-RawSocket) | [Contracts](Stdlib-Net-Socket-RawSocket#detailed-contracts) | [Declarations](Stdlib-Net-Socket-RawSocket#exported-declarations) |
| [`Std.Net.Socket.SeqPacketSocket`](Stdlib-Net-Socket-SeqPacketSocket) | [Contracts](Stdlib-Net-Socket-SeqPacketSocket#detailed-contracts) | [Declarations](Stdlib-Net-Socket-SeqPacketSocket#exported-declarations) |
| [`Std.Net.Socket.SocketOptions`](Stdlib-Net-Socket-SocketOptions) | [Contracts](Stdlib-Net-Socket-SocketOptions#detailed-contracts) | [Declarations](Stdlib-Net-Socket-SocketOptions#exported-declarations) |
| [`Std.Net.Socket.StreamSocket`](Stdlib-Net-Socket-StreamSocket) | [Contracts](Stdlib-Net-Socket-StreamSocket#detailed-contracts) | [Declarations](Stdlib-Net-Socket-StreamSocket#exported-declarations) |
| [`Std.Net.Socket.TcpConnector`](Stdlib-Net-Socket-TcpConnector) | [Contracts](Stdlib-Net-Socket-TcpConnector#detailed-contracts) | [Declarations](Stdlib-Net-Socket-TcpConnector#exported-declarations) |
| [`Std.Net.Socket.TcpServer`](Stdlib-Net-Socket-TcpServer) | [Contracts](Stdlib-Net-Socket-TcpServer#detailed-contracts) | [Declarations](Stdlib-Net-Socket-TcpServer#exported-declarations) |
| [`Std.Net.Socket.TcpSocket`](Stdlib-Net-Socket-TcpSocket) | [Contracts](Stdlib-Net-Socket-TcpSocket#detailed-contracts) | [Declarations](Stdlib-Net-Socket-TcpSocket#exported-declarations) |
| [`Std.Net.Socket.UdpSocket`](Stdlib-Net-Socket-UdpSocket) | [Contracts](Stdlib-Net-Socket-UdpSocket#detailed-contracts) | [Declarations](Stdlib-Net-Socket-UdpSocket#exported-declarations) |
| [`Std.Net.Socket.UnixDatagram`](Stdlib-Net-Socket-UnixDatagram) | [Contracts](Stdlib-Net-Socket-UnixDatagram#detailed-contracts) | [Declarations](Stdlib-Net-Socket-UnixDatagram#exported-declarations) |
| [`Std.Net.Socket.UnixListener`](Stdlib-Net-Socket-UnixListener) | [Contracts](Stdlib-Net-Socket-UnixListener#detailed-contracts) | [Declarations](Stdlib-Net-Socket-UnixListener#exported-declarations) |
| [`Std.Net.Socket.UnixStream`](Stdlib-Net-Socket-UnixStream) | [Contracts](Stdlib-Net-Socket-UnixStream#detailed-contracts) | [Declarations](Stdlib-Net-Socket-UnixStream#exported-declarations) |
| [`Std.Net.TcpListener`](Stdlib-Net-TcpListener) | [Contracts](Stdlib-Net-TcpListener#detailed-contracts) | [Declarations](Stdlib-Net-TcpListener#exported-declarations) |
| [`Std.Net.TcpStream`](Stdlib-Net-TcpStream) | [Contracts](Stdlib-Net-TcpStream#detailed-contracts) | [Declarations](Stdlib-Net-TcpStream#exported-declarations) |
| [`Std.Net.Tls.Acme`](Stdlib-Net-Tls-Acme) | [Contracts](Stdlib-Net-Tls-Acme#detailed-contracts) | [Declarations](Stdlib-Net-Tls-Acme#exported-declarations) |
| [`Std.Net.Tls.DynamicCertResolver`](Stdlib-Net-Tls-DynamicCertResolver) | [Contracts](Stdlib-Net-Tls-DynamicCertResolver#detailed-contracts) | [Declarations](Stdlib-Net-Tls-DynamicCertResolver#exported-declarations) |
| [`Std.Net.Tls.Handshake`](Stdlib-Net-Tls-Handshake) | [Contracts](Stdlib-Net-Tls-Handshake#detailed-contracts) | [Declarations](Stdlib-Net-Tls-Handshake#exported-declarations) |
| [`Std.Net.Tls.Spiffe`](Stdlib-Net-Tls-Spiffe) | [Contracts](Stdlib-Net-Tls-Spiffe#detailed-contracts) | [Declarations](Stdlib-Net-Tls-Spiffe#exported-declarations) |
| [`Std.Net.Tls.TlsAcceptor`](Stdlib-Net-Tls-TlsAcceptor) | [Contracts](Stdlib-Net-Tls-TlsAcceptor#detailed-contracts) | [Declarations](Stdlib-Net-Tls-TlsAcceptor#exported-declarations) |
| [`Std.Net.Tls.TlsConfig`](Stdlib-Net-Tls-TlsConfig) | [Contracts](Stdlib-Net-Tls-TlsConfig#detailed-contracts) | [Declarations](Stdlib-Net-Tls-TlsConfig#exported-declarations) |
| [`Std.Net.Tls.TlsConnector`](Stdlib-Net-Tls-TlsConnector) | [Contracts](Stdlib-Net-Tls-TlsConnector#detailed-contracts) | [Declarations](Stdlib-Net-Tls-TlsConnector#exported-declarations) |
| [`Std.Net.Tls.TlsStream`](Stdlib-Net-Tls-TlsStream) | [Contracts](Stdlib-Net-Tls-TlsStream#detailed-contracts) | [Declarations](Stdlib-Net-Tls-TlsStream#exported-declarations) |
| [`Std.Net.Transport`](Stdlib-Net-Transport) | [Contracts](Stdlib-Net-Transport#detailed-contracts) | [Declarations](Stdlib-Net-Transport#exported-declarations) |

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

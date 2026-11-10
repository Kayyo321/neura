
## Callbacks

An image's `CALB` table names functions that a host service may call while it serves a request ([NVR](https://github.com/Kayyo321/neura/wiki/NVR#imports-and-optional-source-maps)). The runner passes each request a callback context. Calling a callback runs that function on the same heap and globals, above the frame suspended at the host call, until it returns; the host requests it makes are served the same way, so callbacks can nest. Steps, frames, and heap cells count against the run's limits.

A trap, a process exit, or a failed limit during a callback ends the run. The callback returns nothing to the service, the service's eventual response is ignored, and the run reports how it ended. `nvm::Execution::invoke_callback` is the embedding interface.

`Std.DynamicForeign` registers `neura.ffi.upcall` version 1 with parameters `int64 stub, uint8 list arguments` and result `uint8 list`. `neura.ffi.upcall.create(int64 arena, int64 stub, uint8 list signature) → int64 address` returns a native function pointer, a libffi closure, that packs its arguments as `neura.ffi.call` does and calls the callback with them. It writes the packed result back to native code. A stub is valid only while its arena is open and while a `neura.ffi.call` from the same run is in progress; native code that calls it at any other time ends the process with status 134 and a message on standard error. A variadic signature cannot be an upcall.
# Neura Virtual Machine

The Neura Virtual Machine (NVM) loads and executes a Neura Virtual Runtime image (`.nvr`). An image contains compiled neuron graphs, typed execution metadata, constants, and the host imports needed by the program. This specification defines the NVM execution contract. [NVR format](NVR) defines its binary encoding. [Neuron conversion and execution](Backend) describes the implementation: its run limits, trap reporting, and embedding API, and the decisions the compiler takes where the language is open.

## Toolchain and distribution

```text
hello.nra -> hello.nir -> hello.nvr -> NVM
              typed IR     executable
```

The compiler can materialize versioned NeuraIR as a `.nir` file before optimization. A build from source can pass the same representation internally. Both routes validate NeuraIR, apply the selected IR optimization pipeline, convert the result to neuron graphs and execution metadata, and encode the NVR image. A `.nir` reader must preserve types, symbol identity, region structure, evaluation order, and effects. The human-readable IR listing is a separate interface.

The compiler distribution provides these commands:

```sh
neura emit-ir hello.nra -o hello.nir
neura build hello.nir -o hello.nvr
neura build hello.nra -o hello.nvr
neura run hello.nvr -- arg1 arg2
```

`neura run` loads the image, binds the selected host services, and passes arguments after `--` to the entry function. The loader and executor form a reusable NVM library. A runtime-only distribution uses the same loader and execution contract. A compiled image can move between supported hosts with compatible format and service versions.

## Graphs and continuations

A function has a signature, a frame layout, and an entry block. Each block evaluates a feedforward neuron graph, then takes one declared exit. Blocks can share graph definitions. A loop repeats blocks through continuations. A call uses the callee's entry block and its own frame. The graph produces an exit selector and payload bits. NVM interprets the selected exit descriptor and transports its typed values.

The reference graph profile uses signed 64-bit integer weights and biases, with identity and ReLU activations. For a node, NVM starts with its bias and visits incoming edges in ascending source index order. Each multiplication and running sum uses checked signed 64-bit arithmetic. Overflow traps before the activation. Identity returns the sum. ReLU returns zero for a negative sum and the sum otherwise. Node evaluation order follows the stored topological order. A backend must produce circuits whose intermediate arithmetic is valid for the input domain it claims to support.

An implementation may compute graph output lanes by another method when they equal the reference values and reference evaluation does not overflow. If the method cannot establish both conditions, the reference evaluator supplies the result or reports the overflow.

At the graph boundary, each value occupies a sequence of bit lanes. Lane zero is the least significant bit. Booleans occupy one lane, and integers occupy their declared width with two's-complement encoding for signed values. References cross the graph boundary as 64-bit opaque handles. Every output lane consumed by the selected exit, including the selector, must be exactly zero or one. A selector outside the block's exit table traps. Source arithmetic and comparisons are expressed by the graph, including checks and exits for specified arithmetic failures. Checked neuron arithmetic is an execution rule for the target and is distinct from the source language's arithmetic rules.

A typed value descriptor can read a frame slot, a constant, or an output lane slice. The runtime transports opaque handles and checks their type and validity when used. Language-level equality, arithmetic, and control decisions are represented in the circuit. Execution metadata can transport a constant or a live reference without routing it through a neuron.

### Graph devices

A graph device belongs to one validated image. Each job sends a graph index and its binary input lanes. The device either returns the exact output lanes in the graph's output order or declines the job. A device failure also declines the job. The executor evaluates declined jobs by reference, including the reference evaluator's first-overflow diagnostic. The executor checks selectors and consumed output lanes and owns every continuation, slot, heap object, resource limit, and host interaction. Device evaluation has no host import or trace entry.

A device can expose its graph shapes in image order and a failure reason to a placement wrapper. The wrapper can then omit graphs excluded by the device's table limit or demoted after an escape. An empty shape view leaves the wrapper with the image's static shapes. A failure reason retires the device from automatic placement for the remainder of the run.

Embedders can split a block transition using `Execution::begin_step`, then `finish_step` with exact output lanes or `fail_step` with the reference overflow's node index. Begin charges the block's step and resolves its inputs before evaluation. The input view remains valid until the step is settled. Beginning another step or calling `advance` before settling it is an API error. Finishing with the wrong number of lanes rejects the call and leaves the step open. With no device in `RunLimits`, `advance` evaluates by reference.

## Execution state

Each run owns its call stack, heap, argument values, pending continuation, resource counters, and host bindings. Starting a fresh run creates fresh state from the image and the launch inputs. The root frame receives the UTF-8 argument array. A function frame has typed slots for parameters, locals, and temporaries. Parameters are initialized on entry and remaining slots begin uninitialized. Reading an uninitialized slot traps. The implementation may use a faster internal layout if the observable behavior is identical.

A normal block transition proceeds in this order:

1. Resolve graph inputs from the current frame and constant pool. Check types, initialization, and applicable limits.
2. Evaluate the graph and decode the selector.
3. Resolve the selected exit's values and slot assignments from the pre-transition frame, constants, and graph outputs. Check the exit, payload types, and destinations.
4. Commit the exit's slot assignments in parallel. Perform the exit action. A call, heap request, or host request records its continuation before control transfers.
5. Enter the destination block, return to the caller, or finish execution.

The parallel assignment rule lets a block swap two slots without an ordering dependency. Exit descriptors for unselected paths have no effects. For short-circuit expressions, the right operand's block is reached only when the left operand selects it. Compiling pure alternatives into a single data-selection graph is valid when the source contract permits both computations to be evaluated.

| Exit | NVM action |
| --- | --- |
| Continue | Enter the given block in the current function. |
| Call | Push a frame with the resolved arguments and enter the selected function. The caller retains the resume block and result slot. |
| Return | Pop the frame, place the result in the caller's result slot if present, and enter the caller's resume block. Returning from the entry frame completes the run. |
| State operation | Perform the specified checked heap or global storage operation, store its result if present, and enter the resume block. |
| Host call | Invoke the bound service with the resolved arguments and resume on the declared success or failure continuation. |
| Trap | Terminate the run with the specified runtime fault. |

Direct calls name a function in execution metadata. Dynamic dispatch reads the receiver's type tag and uses a graph-selected exit to choose a compatible function. NVM checks the handle and call signature at the boundary. A graph can implement selection as a decision tree across blocks when a single graph would make the dispatch unnecessarily large.

## Fleets

A fleet advances independent executions of one image. Each member owns its frames, globals, heap, limits, and counters. A round considers a bounded window of unfinished members and may evaluate their graphs together. Only the lowest-numbered member whose final event is still unreported can issue a host request. The caller completes that request or serves a callback before the member continues. Final events arrive in member order. The resulting host requests, responses, and final events follow the same order as running the members one at a time.

The head can always start. Other members can start while the sum of the live heap cells of started, unfinished members is below one member’s heap limit. Admission counts initial constants and arguments as soon as a member starts, including before its first graph step. Once admitted, a member keeps running under its own limits. Aggregate process memory can exceed the limit of one member.

When no device is set and every member has been admitted, the reference executor can group rounds until the head reaches its next event. Other runnable members advance through the same number of rounds, stopping at their first event. Each member sees the same inputs and state, and no admission or host response can intervene during the group. The visible events and round count remain those of the individual rounds.

Fleet admission tracks live heap cells as they change. Finishing a member removes its remaining cells from the total before another member is admitted. With a graph device, the fleet checks graph input reads before placement and can defer packing their lanes until evaluation. The begun frame and constants stay unchanged in between. An automatic device can request full jobs only for the graph indices it selects. Other devices receive the eager batch, and every declined step uses the reference evaluator.

## Heap and objects

Objects have a runtime type tag, a layout ID, and typed field slots. Layouts preserve inherited fields at stable indices. A method receives the object handle as an explicit argument. Each object has independent storage, while calls share the compiled method graphs. Collections use typed element slots, and lengths and indices are checked before accessing storage.

Object and interface references can carry nil as handle zero. The collector skips it, and field access, virtual dispatch, and destruction fault when they try to dereference it. A type test returns false for nil, and a checked cast preserves it. Class identity operations give a live object a sequence number when its default hash or text is first requested. The number stays with the object and depends on request order.

An allocation creates a fresh handle and uninitialized fields. Compiler-generated constructor control flow performs base initialization, field initialization, and the declared constructor body in language order. It exposes the result of `new` on successful completion. If the language permits a field to be initialized later, its slot remains uninitialized until a store. A premature load traps. Field and collection stores validate the handle, layout, index, and value type before changing storage. Compiler-generated destruction calls the required cleanup methods and then invalidates the handle. Freed handles cannot name later allocations. These rules define runtime checks. The language specification defines which source operations are legal and the ordering of language-visible initialization and cleanup.

For example, incrementing a counter runs a block that requests a field read, a graph that adds to the returned value, and a block that requests a field write. The heap handler performs the read and write. The graph supplies the new value.

### Garbage collection

NVM uses a non-moving tracing collector. The root set comprises reference constants, initialized reference globals, and initialized reference slots in every active frame. Temporary references also remain rooted while constructing the launch arguments, allocating a collection from its already evaluated elements, or appending an element after the exit's parallel assignments. NVM traces initialized reference fields by the object's actual layout and visits reference elements in arrays and lists. Text and byte storage are leaves. Numeric slots never keep an object alive even when their bits match its handle.

Allocation and list growth are safepoints. The collector marks reachable entries iteratively and sweeps the remaining entries, including unreachable cycles. It attempts collection before an allocation or growth would exceed the configured heap limit. It also collects at a soft threshold that grows with the retained heap, so workloads that release objects can reclaim space before reaching the hard limit. Collection does not consume program steps and uses no clocks or background threads. An initialized frame slot retains its reference until the slot is overwritten or its frame returns.

The collector caches the reference positions in each encountered object layout and frame signature. Subsequent traces visit those positions directly and skip scalar cells.

Collection reclaims storage without invoking language cleanup methods. An explicit `destroy` runs its compiled cleanup and invalidates the object immediately. Both reclaimed and explicitly destroyed handles remain invalid, and every new allocation receives a fresh handle. The run's handle limit counts all assigned handles independently of reclaimed storage. Programs must keep references in typed storage across safepoints to retain their targets.

## Host calls and standard output

A host import declares a service name, interface version, typed arguments, and typed success and error results. An exit references an import by table index. At load time the runtime binds each declared import to a registered service with the same signature and version under the runner's service policy. Imports grant access only to their bound service. A service that needs filesystem or network access defines that authority in its own interface and configuration.

A request contains the import, resolved arguments, and an execution-local sequence number. NVM issues the request once on entering the selected exit. A completion supplies either the success or error value. The runtime type-checks that value, writes the designated result slot, and enters the corresponding resume block. A suspended run keeps the pending request and resumes from its completion. The execution contract covers a live run. Durable recovery across a process crash requires a separate persistence protocol.

Host services receive copied values. Booleans, integers, and floating-point values keep their slot bit encodings. Strings, byte arrays, and byte lists use byte sequences. Other arrays and lists transfer their elements in index order, and tuples transfer their fields in declaration order. Host signatures describe these shapes recursively without image-local type indices. The runner limits copied elements and bytes using the heap-cell budget and bounds the nesting depth. Handles remain inside NVM, and a compound completion allocates fresh VM storage with its partially constructed values rooted until completion.

A terminal completion from `neura.process.exit` version 1 ends only the current NVM execution with the supplied status from 0 through 255. A recorder stores that completion and replay returns the same terminal result without invoking the service. The runner must register the service before an image can call it.

The standard console service is `neura.console.write_line` version 1. It takes a UTF-8 string and returns either success with no payload or an unsigned 32-bit error code. Error code 1 denotes a failed write. It emits the string's bytes followed by a single LF byte, independent of platform newline conventions. A host write can partially succeed before reporting failure. The runtime reports the failure without retrying. The language library determines how that result appears to source code.

### Host services

`neura run` and `neura test` register these services. Each is version 1, and each failure is a `uint32` error code: 1 `NotFound`, 2 `PermissionDenied`, 3 `AlreadyExists`, 4 `InvalidInput`, 5 `Interrupted`, 6 `UnexpectedEnd`, 7 `NotADirectory`, 8 `IsADirectory`, 9 `DirectoryNotEmpty`, 10 `ConnectionRefused`, 11 `ConnectionReset`, 12 `TimedOut`, 13 `Unsupported`, 14 `CapabilityDenied`, and 100 for any other failure ([Stdlib](Stdlib-Design#45-host-services) maps codes to `IoErrorKind`). File and socket IDs are opaque `int64` values numbered from 1 in each run and never reused within it.

| Service | Parameters | Success | Behavior |
| --- | --- | --- | --- |
| `neura.console.write_line` | text | void | Writes the text and an LF; error 1 when the write fails |
| `neura.console.write` | int32 stream (1 output, 2 error), uint8 list | int32 | Writes a prefix of the bytes and returns its length; the library repeats partial writes |
| `neura.console.read` | int32 maximum | uint8 list | At most the maximum, at least one byte; empty at end of input |
| `neura.fs.open` | text path, int32 mode | int64 ID | Mode is the `OpenMode` ordinal: Read, Write, Append, ReadWrite, CreateNew |
| `neura.fs.read` | int64 ID, int32 maximum | uint8 list | Empty at end of file |
| `neura.fs.write` | int64 ID, uint8 list | int32 | The count written |
| `neura.fs.seek` | int64 ID, int64 offset, int32 origin | int64 | Origin is the `SeekOrigin` ordinal: Start, Current, End |
| `neura.fs.close` | int64 ID | void | Releases the ID |
| `neura.fs.stat` | text path, bool follow links | [int32 kind, int64 size, int64 seconds, int32 nanoseconds] | Kind is the `FileKind` ordinal: File, Directory, Other |
| `neura.fs.list` | text path | text list | Entry names sorted by their UTF-8 bytes |
| `neura.fs.make_directory` | text path | void | |
| `neura.fs.remove` | text path, bool directory | void | Removes a file, or an empty directory when the flag is set |
| `neura.fs.rename` | text from, text to | void | |
| `neura.clock.wall` | | [int64 seconds, int32 nanoseconds] | Since 1970-01-01T00:00:00Z |
| `neura.clock.monotonic` | | int64 nanoseconds | Never decreases within a run |
| `neura.clock.sleep` | int64 nanoseconds | void | |
| `neura.entropy.fill` | int32 count | uint8 list | Operating-system entropy |
| `neura.env.get` | text name | [bool found, text value] | |
| `neura.process.exit` | int32 status | void | Terminal: ends the run with the status |
| `neura.net.connect` | text host, int32 port, int64 timeout nanoseconds | int64 ID | Blocking TCP connect; 0 means no timeout |
| `neura.net.listen` | text host, int32 port | int64 ID | Port 0 asks for a free port |
| `neura.net.local_port` | int64 ID | int32 | The listener's bound port |
| `neura.net.accept` | int64 ID | int64 ID | |
| `neura.net.read` | int64 ID, int32 maximum, int64 timeout nanoseconds | uint8 list | Empty at end of stream |
| `neura.net.write` | int64 ID, uint8 list | int32 | The count written |
| `neura.net.close` | int64 ID | void | Closes a stream or a listener |
| `neura.ffi.abi` | | [text triple, uint8 list] | The target triple, then the size and alignment of `bool`, `char`, `short`, `int`, `long`, `long long`, `float`, `double`, `void*`, `size_t`, and `wchar_t`, then 1 when `char` is signed and 1 when the byte order is little-endian |
| `neura.ffi.arena.open` | | int64 ID | A native memory arena |
| `neura.ffi.arena.close` | int64 ID | void | Frees the arena's memory and unloads its libraries |
| `neura.ffi.alloc` | int64 arena, int64 size, int64 alignment | int64 address | Zeroed; arena 0 is the global arena, freed when the run ends; alignment is a power of two up to 4,096 |
| `neura.ffi.library.open` | int64 arena, text path | int64 ID | A path with a slash loads that file; another name uses the platform's search; error 1 when it cannot be loaded |
| `neura.ffi.library.default` | | int64 ID | The libraries already in the process |
| `neura.ffi.library.embedded` | | int64 ID | The image's embedded native library; error 1 when the image has none |
| `neura.ffi.symbol` | int64 library, text name | int64 address | Error 1 when absent |
| `neura.ffi.read`, `neura.ffi.write` | int64 address, int64 length or uint8 list | uint8 list or void | At most 16 MiB |
| `neura.ffi.strlen` | int64 address, int64 maximum | int64 | Bytes before the first NUL |
| `neura.ffi.call` | int64 function, uint8 list signature, uint8 list arguments | uint8 list | A call through libffi; the formats are below |
| `neura.ffi.upcall.create` | int64 arena, int64 stub, uint8 list signature | int64 address | An upcall stub ([Callbacks](#callbacks)) |

**Native calls.** A `neura.ffi.call` signature is a version byte (1), a little-endian `uint16` fixed-argument count (65535 for a function that is not variadic), a `uint16` argument count, the result type, and each argument type. A type is one tag: 0 void, 1 `_Bool`, 2 through 9 the signed and unsigned 8-, 16-, 32-, and 64-bit integers, 10 `float`, 11 `double`, 12 a pointer, or 13 a struct followed by a `uint16` member count and the member types. Arguments are the values' bytes back to back in native byte order, each exactly its type's size, and the result is the result type's bytes. A variadic argument must not be a `float` or an integer narrower than `int`. A malformed signature or argument list fails with code 4 before the call. The runner frees every arena and unloads every library when a run ends. Native code runs in the runner's process, outside the NVM's checks ([Foreign interop](https://github.com/Kayyo321/neura/wiki/ForeignInterop)).
| `neura.ffi.abi` | | [text triple, uint8 list] | The target triple, then the size and alignment of `bool`, `char`, `short`, `int`, `long`, `long long`, `float`, `double`, `void*`, `size_t`, and `wchar_t`, then 1 when `char` is signed and 1 when the byte order is little-endian |
| `neura.ffi.arena.open` | | int64 ID | A native memory arena |
| `neura.ffi.arena.close` | int64 ID | void | Frees the arena's memory and unloads its libraries |
| `neura.ffi.alloc` | int64 arena, int64 size, int64 alignment | int64 address | Zeroed; arena 0 is the global arena, freed when the run ends; alignment is a power of two up to 4,096 |
| `neura.ffi.library.open` | int64 arena, text path | int64 ID | A path with a slash loads that file; another name uses the platform's search; error 1 when it cannot be loaded |
| `neura.ffi.library.default` | | int64 ID | The libraries already in the process |
| `neura.ffi.library.embedded` | | int64 ID | The image's embedded native library; error 1 when the image has none |
| `neura.ffi.symbol` | int64 library, text name | int64 address | Error 1 when absent |
| `neura.ffi.read`, `neura.ffi.write` | int64 address, int64 length or uint8 list | uint8 list or void | At most 16 MiB |
| `neura.ffi.strlen` | int64 address, int64 maximum | int64 | Bytes before the first NUL |
| `neura.ffi.call` | int64 function, uint8 list signature, uint8 list arguments | uint8 list | A call through libffi; the formats are below |

**Native calls.** A `neura.ffi.call` signature is a version byte (1), a little-endian `uint16` fixed-argument count (65535 for a function that is not variadic), a `uint16` argument count, the result type, and each argument type. A type is one tag: 0 void, 1 `_Bool`, 2 through 9 the signed and unsigned 8-, 16-, 32-, and 64-bit integers, 10 `float`, 11 `double`, 12 a pointer, or 13 a struct followed by a `uint16` member count and the member types. Arguments are the values' bytes back to back in native byte order, each exactly its type's size, and the result is the result type's bytes. A variadic argument must not be a `float` or an integer narrower than `int`. A malformed signature or argument list fails with code 4 before the call. The runner frees every arena and unloads every library when a run ends. Native code runs in the runner's process, outside the NVM's checks ([Foreign interop](https://github.com/Kayyo321/neura/wiki/ForeignInterop)).

A read requests at most 1 MiB, and so does an entropy request. A run holds at most 1,024 open file IDs and 1,024 open socket IDs; an open beyond that fails with code 100. When a run ends, by completion, trap, or process exit, the runner closes every file and socket it left open. `neura test` does the same after each test.

**Capabilities.** The console services and `neura.process.exit` are always available. Every other service belongs to a group that the runner grants with an option: `--allow-fs` (optionally `--allow-fs=prefix,...`), `--allow-net`, `--allow-env`, `--allow-clock`, `--allow-entropy`, and `--allow-native`. A service that is not granted still binds, and each call fails with code 14. With path prefixes, the provider resolves each path to an absolute path the way the operating system would, following symbolic links, and allows it only at or below a resolved prefix; operations on an entry itself (remove, rename, make directory, stat without following links, create) check the resolved parent and the final name. The check and the operating-system call are not atomic.

A failed console write traps with reason 3 when it comes from `Console.print_line`; the other console forms return the failure as an `Io` error. For `console.print_line("Hello, World!")`, the image stores the string in its constant pool. A graph selects a host-call exit whose argument names that constant. The bound console handler writes the line and NVM resumes at the exit's success block. The entry function's `ExitCode` becomes the process status. A trap yields a diagnostic, and `nvm::exit_status` gives the process status for its class, the `Std.Program.ExitCode` variant that names it ([Errors](https://github.com/Kayyo321/neura/wiki/Errors)). The embedding API exposes the full status or structured trap independently of the platform's exit-code range.

The test assertion services `neura.test.assert` and `neura.test.assert_equal_int32` are retired along with the built-in `Test` module. An image built earlier that imports them fails to bind, before it runs; an earlier image whose only import is `neura.console.write_line` still runs.

## Determinism, limits, and observability

Given the same validated image, initial inputs, resource limits, and host completion trace, a run yields the same exit selections, effects, state updates, and terminal outcome. Clock readings, file contents, and similar external responses enter through the host boundary. Tests can supply those responses from a trace and verify the emitted request sequence. `nvm::record` runs an image with bound services and records each request, as the import's name and version with its arguments, together with the response and the terminal outcome. `neura run --record <file>` writes such a trace as text, and `neura run --replay <file>` answers every request from one without invoking any service or needing any capability. The text form starts with `neura-trace 1`, or `neura-trace 2` when it records callbacks; each `request <import> <version> <arguments>` line is followed by `ok <value>`, `error <value>`, or `exit <status>`, and the file ends with `outcome completed <status>` or `outcome trap <kind> <reason> "<message>"`. Values are unsigned decimal slot bits, quoted byte strings with `\xHH`, `\n`, `\r`, `\t`, `\"`, and `\\` escapes, or bracketed lists; `#` lines are comments. Between a request line and its response, `callback <name> <version> <arguments>` opens a callback the service made, followed by the requests the callback made and `return <value>`, or `ended` when the run ended during it. A replay calls each recorded callback with its recorded arguments before supplying the request's response, and checks that the callback makes the recorded requests and returns the recorded value. A replay reproduces the console output the recorded requests wrote, and a divergence ends with status 1 and a description of the first differing request. `nvm::replay` runs an image, answering each request from a recorded trace instead of real services, and reports whether the image made the same requests in the same order and ended the same way. Requests match by import name and version, not table index, so two images that number their imports differently can be compared. The decompiler tests use this to compare an image with one regenerated from its decompilation. The reference executor runs one block at a time and waits for each selected host call before resuming. Fleets may advance independent runs together while preserving their serial order where it is observable.

The loader applies size and count limits before allocating image structures. The runner enforces declared bounds on frame depth, live heap storage, and execution steps. Exhaustion traps at the operation that exceeds the configured limit. Limit checks occur before the affected allocation or state mutation. A trap stops further program execution and leaves any completed external effects visible.

Diagnostics identify the runtime fault and, when execution has begun, the function and block. Optional source maps add source locations. A trace can expose block entries, graph exits, and requests and responses. Inspection and decompilation use the executable graph and continuation tables, with source maps as supplemental information.

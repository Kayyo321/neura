# Neura Virtual Runtime image format

An NVR image is a binary executable for the [Neura Virtual Machine](NVM.md). The file extension is `.nvr`. The image is the output of `IRtoNeuronConversion` and serialization. Its executable records contain everything needed to evaluate the compiled program with a compatible NVM and its declared host services.

## Encoding and compatibility

Version 1 uses fixed-width little-endian integers. Signed integers use two's-complement encoding. There is no implicit padding. `u8`, `u16`, `u32`, and `u64` denote unsigned integers of those widths. `i64` denotes a signed 64-bit integer. A `vector<T>` is a `u32` element count followed by that many consecutive records. `text` is a byte vector containing valid UTF-8. `bytes` is a byte vector with arbitrary contents. Record fields appear in the order listed.

A reference into a table is its zero-based `u32` index. `0xffffffff` marks absence only in fields declared optional. Graph wires, blocks, exits, fields, and frame slots have their own local index spaces. Table and local counts must leave the reserved index available. The format version governs binary encoding. The graph profile governs evaluation semantics. Host service versions govern the contracts for external operations. These version axes are independent.

### File header

| Field | Type | Value |
| --- | --- | --- |
| Magic | 4 bytes | ASCII `NEUR` |
| Major | u16 | 1 |
| Minor | u16 | 0 through 6 |
| Graph profile | u32 | 1 for checked integer identity and ReLU graphs |
| Section count | u32 | Number of directory records |
| File size | u64 | Total size in bytes, including the header and directory |

The section directory immediately follows the header. Each directory record is `tag[4], flags:u32, offset:u64, size:u64`. Tags use four printable ASCII characters and compare bytewise. Offsets are absolute byte offsets. Bit zero of flags means the section is required for execution. Other flag bits are reserved and zero in version 1.0. Sections follow the directory in ascending tag order, without gaps or overlaps. The directory uses the same order and contains each tag at most once.

### Versions 1.0 through 1.6

Minor version 1 adds float and bfloat type kinds (tags 9 and 10), the read-version state operation (code 14), and trap reason 5. Minor version 2 adds an interface vector to each layout record, a nil object constant, a message-trap exit, and state operations 15 through 22. Minor version 3 adds the repeated collection allocation state operation (code 23). Minor version 4 adds the `CALB` callback table and the `NATV` embedded native library. Minor version 5 adds scheduler exits, polls, and the `TRNS` transferable-layout table. Minor version 6 adds the kernel state operation (code 24) and the `KERN` kernel table ([Kernels](#kernels-16)). The graph profile remains 1.

- A writer emits the lowest minor version whose features the image uses. A writer that re-encodes a loaded image keeps at least the minor version the image declared.
- The loader accepts major version 1 with minor versions 0 through 6. A feature newer than the version declared by the image makes the image malformed.
- An unknown required section or graph profile produces an unsupported-image error. A recognized version can carry unknown optional sections. The loader keeps their bytes so that re-encoding the image reproduces them; execution ignores them. A known section must carry the required flag given in the table below. Executable sections never reference optional sections.

| Tag | Contents | Required |
| --- | --- | --- |
| `CALB` | Callback records (1.4); present only when the image has callbacks | Yes, when present |
| `CONS` | Constant records | Yes |
| `FUNC` | Function, frame, block, and exit records | Yes |
| `GLOB` | Global slot declarations | Yes |
| `GRPH` | Neuron graph records | Yes |
| `IMPT` | Host import declarations | Yes |
| `KERN` | Kernel records (1.6); present exactly when the image has a kernel exit | Yes, when present |
| `LAYO` | Object layout records | Yes |
| `LIFT` | Lifted forms of blocks, as hints for a faster tier (not interpreted by the loader; never trusted) | Optional |
| `META` | Entry function index as one `u32` | Yes |
| `NATV` | The embedded native library (1.4); present only when the image has C imports | Yes, when present |
| `NAME` | Source names for tools (provenance; not interpreted by the loader) | Optional |
| `SMAP` | Source locations for function blocks | Optional |
| `TRNS` | Transferable layout indices (1.5) | Yes, when present: required for a scheduler exit or poll, and present exactly when the image has one |
| `TYPE` | Type records | Yes |

The listed sections other than `META` and `NATV` store vectors. A required table with zero entries still carries a zero count. Readers must consume every byte in each recognized section.

## Types, constants, and storage

A type record starts with `kind:u8`. Its remaining fields depend on that tag.

| Kind | Tag | Fields after tag | Graph lanes |
| --- | --- | --- | --- |
| Void | 0 | None | 0 |
| Boolean | 1 | None | 1 |
| Unsigned integer | 2 | `width:u8`, one of 8, 16, 32, 64 | Width |
| Signed integer | 3 | `width:u8`, one of 8, 16, 32, 64 | Width |
| UTF-8 text | 4 | None | 64 |
| Bytes | 5 | None | 64 |
| Object reference | 6 | `layout:u32` | 64 |
| Array reference | 7 | `element_type:u32` | 64 |
| List reference | 8 | `element_type:u32` | 64 |
| Float (1.1) | 9 | `width:u8`, one of 16, 32, 64: IEEE 754 binary16, binary32, binary64 | Width |
| BFloat (1.1) | 10 | `width:u8`, which is 16: an 8-bit exponent and a 7-bit fraction | 16 |

Void is valid as a result type and has no slot, constant, or value descriptor. Signedness and width are part of the type. Assignments and calls require structural type equality, or a declared object subtype where a base type is expected. Arrays and lists are invariant in their element type. Object subtyping follows parent and interface links. Profile 1 uses bit vectors at graph ports, including for opaque handles. A float value's lanes are its IEEE encoding, least significant bit first, so graphs compute float arithmetic bit by bit ([Numbers](../lang/Numbers.md)).

A constant record contains `type:u32, payload:bytes`. A Boolean payload contains one byte, zero or one. An integer or float payload contains exactly its declared width in bits divided by eight, little-endian. Every float bit pattern, including any NaN, is a valid payload. Text and bytes use their literal contents and are immutable. In 1.2, an empty payload for an object type denotes nil. Nonzero object handles and collection handles are runtime values.

A layout record contains `parent:u32 optional, field_types:vector<u32>`. Version 1.2 appends `interfaces:vector<u32>` to every layout record. Each interface link is a unique fieldless layout other than the parent. The union of both link kinds is acyclic. The field vector includes the parent's fields as a prefix with identical types and indices. The layout index is its runtime type tag. A global record contains `type:u32, initializer:u32 optional`. An initializer names a matching constant. A global without one begins uninitialized. Compiler-generated entry blocks perform nonconstant program initialization before invoking application logic.

NVM assigns reference handles in a deterministic order, starting with materialized reference constants in table order, then each launch argument's text in order, then the argument array, and then allocations in execution order. Zero is reserved for absence and never allocated. In 1.2 an object-typed descriptor may transport zero as nil. Operations that dereference it trap, except the explicit type test and cast described below. The runner never reuses a destroyed handle and traps if its handle space is exhausted.

## Graphs, functions, and exits

A graph record contains `input_lanes:u32, nodes:vector<Node>, outputs:vector<u32>`. Input wires have indices zero through `input_lanes - 1`. Node outputs follow the input wires in node order. Each node contains `activation:u8, bias:i64, edges:vector<Edge>`. Activation 0 is identity and 1 is ReLU. Each edge is `source:u32, weight:i64`. Edges are sorted by source index and have unique sources. A node can refer only to earlier wires. The output vector lists wire indices in logical output-lane order and can repeat a wire.

A function record contains `parameter_count:u32, result_type:u32, frame_types:vector<u32>, blocks:vector<Block>`. The first parameter-count frame slots hold parameters in declaration order. Block zero is the function entry. The image's entry function takes one UTF-8 text array and returns a signed 32-bit integer.

A value descriptor contains `kind:u8, type:u32, index:u32`. Kind 0 reads frame slot `index`. Kind 1 reads constant `index`. Kind 2 decodes a value starting at logical output lane `index`, using the declared type's width. The descriptor type must match the source slot or constant type for kinds 0 and 1. Consumers validate the decoded form and check reference handles as needed. A reference decoded from lanes must name a live handle whose runtime type is compatible with the descriptor type, or the run traps; a dispatch block uses this check to pass a receiver at the parameter type of the selected function.

A block record contains `graph:u32, inputs:vector<Value>, selector_start:u32, exits:vector<Exit>`. The inputs are concatenated in list order to fill the graph's input lanes. Only frame slots and constants may supply graph inputs. A block has at least one exit. With one exit, `selector_start` is zero and the runtime selects exit zero directly. Otherwise, it decodes an unsigned exit index starting at `selector_start`, using the minimum number of bits needed to represent the highest exit index.

An exit begins with `kind:u8, assignments:vector<Assignment>`. An assignment is `destination_slot:u32, value:Value`. Each destination appears at most once. All values and action arguments resolve against the same pre-transition state.

| Exit | Tag | Fields after assignments |
| --- | --- | --- |
| Continue | 0 | `resume_block:u32` |
| Call | 1 | `function:u32, arguments:vector<Value>, result_slot:u32 optional, resume_block:u32` |
| Return | 2 | One `Value` for a nonvoid function |
| State operation | 3 | `operation:u8, immediate:u32, arguments:vector<Value>, result_slot:u32 optional, resume_block:u32` |
| Host call | 4 | `import:u32, arguments:vector<Value>, success_slot:u32 optional, success_block:u32, error_slot:u32 optional, error_block:u32` |
| Trap | 5 | `reason:u32` |
| Message trap (1.2) | 6 | `arguments:vector<Value>`, exactly one text value, implicit reason 3 |
| Scheduler operation (1.5) | 7 | `operation:u8, target:u32, arguments:vector<Value>, result_slot:u32 optional, resume_block:u32` |

Resume blocks belong to the current function. For every exit that produces a result (a call, a state operation, a scheduler operation, and both continuations of a host call), a void result requires an absent slot, and a nonvoid result requires a slot to which the result is assignable. Return values and arguments must be assignable to their declared types. A trap exit has no assignments. Reason codes 1 through 4 designate source arithmetic failure, source bounds failure, explicit program failure, and invalid dispatch. Reason 5 (1.1) designates a list modified during for-each. Other codes require a format revision. Loader and runtime faults have separate diagnostics.

## Scheduler operations (1.5)

Tasks, scopes, channels, timers, guards, and isolated parallel items are NVM scheduler state, reached through one exit kind, like the heap is reached through state operations. Neuron graphs still compute every arithmetic result, comparison, and branch decision, including which exit follows an operation's outcome. The scheduler is not a host service: it has no import and needs no capability, and the run is determined by the image, the inputs, the limits, and the host completion trace. The language rules that produce these operations are in [Concurrency](../lang/Concurrency.md).

### The exit record and the poll bit

A scheduler exit has tag 7. After its assignments it holds `operation:u8, target:u32, arguments:vector<Value>, result_slot:u32 optional, resume_block:u32`. The resume block belongs to the current function. As for every exit, the assignments and the arguments resolve against the same pre-transition state, and the result is written to the slot after the operation completes, before the resume block starts. An operation that parks the task completes later, when the scheduler wakes it.

Bit 7 (`0x80`) of the tag byte of a Continue or Call exit marks a **poll**, giving the tag bytes 128 and 129 with the usual record fields. Any other exit kind rejects the bit. A poll adds no block. It counts one language operation toward the running task's quantum after the exit's transition: when the continue is taken, or when the callee's frame is pushed. When the task has counted `--task-quantum` polls (default 1,024) since it was last scheduled, and another task is runnable, it moves to the back of the runnable queue; with nothing else runnable the count restarts and the task continues. The compiler puts a poll on every call and every loop back edge of a program that spawns, so a count is per language operation and does not depend on how conversion shaped the blocks.

### Operations

The assembly names are the spellings `disassemble` writes and `assemble` reads. A result slot marked *optional* may be absent; for every other operation a void result requires an absent slot and any other result requires a slot of the type shown.

| Code | Assembly name | Target | Arguments | Result slot |
| --- | --- | --- | --- | --- |
| 0 | `scope-enter` | Zero | None | Optional `uint64`: the new scope's identity, for a `Scope` handle |
| 1 | `scope-exit` | Zero | None | Absent |
| 2 | `spawn` | Function index | One value per parameter of the target, each of a transferable type | Optional `uint64`: the task's identity. Absent: the task has no handle and the scheduler releases it when it finishes |
| 3 | `join` | Zero | `uint64` task identity | The child's result; absent exactly when the child's result is void |
| 4 | `is-finished` | Zero | `uint64` task identity | `bool` |
| 5 | `scope-cancel` | Zero | `uint64` scope identity | Absent |
| 6 | `scope-is-cancelled` | Zero | `uint64` scope identity | `bool` |
| 7 | `scope-set-deadline` | Zero | `uint64` scope identity, `int64` deadline | Absent |
| 8 | `scope-deadline` | Zero | `uint64` scope identity | `int64` |
| 9 | `task-cancelled` | Zero | None | `bool` |
| 10 | `task-deadline` | Zero | None | `int64` |
| 11 | `take` | Zero | None | Required, of the type of the value taken |
| 12 | `channel-open` | Type index of the channel's element type | An object reference to open as the channel, `int64` capacity | Absent |
| 13 | `channel-send` | Zero | The channel, a transferable value | `uint32` outcome |
| 14 | `channel-receive` | Zero | The channel | `uint32` outcome |
| 15 | `channel-try-send` | Zero | The channel, a transferable value | `uint32` outcome |
| 16 | `channel-try-receive` | Zero | The channel | `uint32` outcome |
| 17 | `channel-close` | Zero | The channel | Absent |
| 18 | `timer-open` | Zero | An object reference to open as the timer, `int64` deadline | Absent |
| 19 | `timer-wait` | Zero | The timer | `uint32` outcome |
| 20 | `select` | Flags: 0, or 1 when a default arm follows the listed arms | The arms (see below) | `uint64`: the arm in the low 32 bits, its outcome in the high 32 |
| 21 | `guard-open` | Zero | An object reference to open as the guard, the guarded value (any type) | Absent |
| 22 | `guard-enter` | Zero | The guard | Required, of a type that accepts the guarded value |
| 23 | `guard-exit` | Zero | The guard | Absent |
| 24 | `parallel-start` | Function index of the item | The item's single argument, `int64` step limit, `int64` heap-cell limit | `uint64`: the item's identity |
| 25 | `parallel-finish` | Zero | A list of `uint64` item identities | An array of the items' results |

A deadline and a timer's time are readings of the monotonic clock in nanoseconds, and `int64` maximum (`9223372036854775807`) means no deadline. A channel, timer, or guard is an ordinary object that the scheduler opens: the object keeps its layout and fields, and the scheduler attaches a record to its handle, which is released when the object is collected or freed. A scope, task, or item identity is an unsigned 64-bit number with no distinct static type.

**What each operation does.**

- `scope-enter` starts a scope owned by the running function frame. `scope-exit` waits until every task spawned in the scope since its `scope-enter`, and every descendant of those tasks, has finished; the task is parked while any is live. A function must not return with a scope open.
- `spawn` creates a task that starts at the target function with the arguments, and appends it to the runnable queue. The parent continues. The task belongs to the innermost open scope of the frame, and its cancellation context is that scope joined with the context the parent inherited.
- `join` returns the child's result, parking the parent until the child has finished; joining again returns the same value. `is-finished` never waits.
- `scope-cancel` requests cancellation of the scope's tasks and their descendants, and is idempotent. A request wakes each parked `channel-send`, `channel-receive`, `timer-wait`, and `select` of those tasks with outcome 2; it never interrupts a running task and never cancels a `join`. `task-cancelled` reports whether the running task's context is cancelled. `scope-deadline` and `task-deadline` return the earlier of the scope's own deadline and the ones it inherited, or the maximum when there is none. `scope-set-deadline` replaces the scope's own deadline; when the monotonic clock reaches it the scheduler cancels the scope.
- `channel-open` registers the object as a channel of the element type with the capacity (0 is a rendezvous). A send completes when a receiver is parked, or when the buffer has room; otherwise `channel-send` parks the task and `channel-try-send` returns outcome 3. A receive takes the oldest buffered value, then the value of the longest-parked sender; otherwise `channel-receive` parks and `channel-try-receive` returns outcome 4. When a receive completes with outcome 0, the value waits for the running task, and the next `take` exit with a slot of an assignable type produces it exactly once. `channel-close` is idempotent: a later send reports outcome 1, buffered values are still received, a receive on an empty closed channel reports outcome 1, and senders parked at the close wake with outcome 1 and their values are dropped.
- `timer-open` and `timer-wait` work the same way for a one-shot timer: `timer-wait` returns outcome 0 once the timer has fired and parks until then.
- `guard-open` stores a value under exclusive access. `guard-enter` gives the value to one task at a time and parks later tasks first come, first served, and `guard-exit` passes it to the next. That wait is not cancellable.
- `select` commits exactly one arm (below).
- `parallel-start` and `parallel-finish` run an item to completion in its own state, with its own heap and no scheduler, under the given limits, and `parallel-finish` returns the results in the order of the list, publishing the lowest-index trap if an item trapped.

**Outcome codes.** An operation that can park or find nothing to do returns a `uint32`:

| Code | Name | Returned by |
| --- | --- | --- |
| 0 | Done | A send, receive, or timer wait that completed; the arm of a join or default that committed |
| 1 | Closed | A send or receive on a closed channel (a receive only once the buffer is empty) |
| 2 | Cancelled | A parked send, receive, timer wait, or select arm that cancellation woke, or one that would park when the context is already cancelled |
| 3 | Full | `channel-try-send` on a channel with no room and no parked receiver |
| 4 | Empty | `channel-try-receive` on a channel with nothing buffered and no parked sender |

**Select arms.** The `arguments` of a select are its arms in source order. Each arm begins with a `uint32` constant (a Constant value, not a slot) that names its kind, followed by the operands of that kind:

| Kind | Name | Operands |
| --- | --- | --- |
| 0 | Receive | The channel |
| 1 | Send | The channel, a transferable value |
| 2 | Join | A `uint64` task identity |
| 3 | Timer | The timer |

The scheduler scans the arms cyclically starting at the arm numbered `selects mod n`, where `selects` is the number of selects the running task has committed and `n` the number of listed arms, and commits the first arm that is ready: a receive that finds a value or a closed channel, a send that completes or finds the channel closed, a join of a finished task, or a timer that has fired. If none is ready and the flag is 1, the default arm commits with index `n`. If none is ready and the task's context is cancelled, the first non-join arm in scan order commits with outcome 2. Otherwise the task parks on every arm and the first operation to complete for it commits. A committed receive or join leaves its value for a `take`. A commit counts toward `selects` whichever arm it was.

### What the loader checks and what the scheduler checks

The loader checks every scheduler exit before the image runs. The scheduler checks what only the run can know.

| Operation | Loader (static) | Scheduler (run time) |
| --- | --- | --- |
| Every operation | The operation code is below 26; the target is zero unless the operation is `spawn`, `channel-open`, `select`, or `parallel-start`; the number of arguments; the types of arguments and of the result slot; the resume block exists | A violation of the contract below is an *invalid task operation*, status 132 |
| `scope-enter` | No arguments; the slot, if present, is `uint64` | The heap budget accepts the scope's cell |
| `scope-exit` | No arguments, no slot | The running frame has an open scope; a function returns with none open |
| `spawn` | The function exists; one argument per parameter, each assignable to its parameter; every argument and parameter type is transferable; the slot, if present, is `uint64` | The frame has an open scope; the live task count is below `--max-tasks` (else the task limit, 137); every object reachable through written fields of an argument has a marked layout; the heap budget accepts the task record and its frame |
| `join` | One `uint64` argument; a slot that exists, if present | The identity is a live child of the running task (not zero, not released, not another task's); the slot is absent exactly when the child's result is void, and otherwise accepts it |
| `is-finished` | One `uint64` argument; a `bool` slot | The identity is a live child of the running task |
| `scope-cancel`, `scope-is-cancelled`, `scope-set-deadline`, `scope-deadline` | The arguments and slot types shown | The scope identity names an open scope that the running task owns |
| `task-cancelled`, `task-deadline` | No arguments; the slot type shown | None |
| `take` | No arguments; a slot that exists | A received or joined value is waiting and its type is assignable to the slot's |
| `channel-open` | The target is a transferable, non-void type index; an object argument and an `int64` capacity | The object is not already a channel, timer, or guard; the capacity is not negative; the heap budget accepts the channel's record |
| `channel-send`, `channel-try-send` | An object and a transferable value; a `uint32` slot | The object is an open channel; the value's type is assignable to the channel's element type; every object reachable through written fields has a marked layout; a buffered value stays within `--max-queued-messages` (else 137) and the heap budget |
| `channel-receive`, `channel-try-receive`, `channel-close` | An object argument; a `uint32` slot, except for close | The object is an open channel |
| `timer-open` | An object and an `int64`; no slot | The object is not already a scheduler object; the heap budget accepts the record |
| `timer-wait` | An object; a `uint32` slot | The object is an open timer |
| `select` | The flag is 0 or 1; every arm begins with a `uint32` constant below 4 and has the operands of its kind; at least one arm; a `uint64` slot | Before any arm commits: each channel is open, each sent value is assignable and marked, each joined task is a live child, each timer is open |
| `guard-open` | An object and a value of any type; no slot | The object is not already a scheduler object; the heap budget accepts the record |
| `guard-enter` | An object; a slot that exists | The object is an open guard; the slot accepts the guarded value; the task does not already hold the guard, else the program trap `invalid state: guard already held` (reason 3, status 134) |
| `guard-exit` | An object; no slot | The running task holds the guard |
| `parallel-start` | The function exists and takes one parameter; the argument is assignable to it; the argument and the function's result are transferable and the result is not void; two `int64` limits; a `uint64` slot | Both limits are positive; every object reachable from the argument has a marked layout and is not a channel, timer, or guard; the heap budget accepts the item. An item that issues a host request is an invalid task operation |
| `parallel-finish` | One list of `uint64`; an array slot | Each identity names an item started and not yet finished; each result is assignable to the array's element type; an item's trap is the run's trap, with the item's index |

Runtime violations of the first kind trap with the diagnostic `invalid task operation: ` and a reason, and exit with status 132. The diagnostics that name a task or spawn chain add it in parentheses. The limits use their own kinds: `task limit exceeded`, `queued message limit exceeded`, and `pending request limit exceeded`, each with status 137, and each checked before any state changes.

### Transferable layouts

The required `TRNS` section stores a `vector<u32>` of strictly ascending layout indices. It may be empty. The section is present exactly when the image has tasks: the loader requires it for a scheduler exit or a poll, and the writer declares at least version 1.5 whenever it writes the table (1.6 when the image also calls a kernel). A marked layout has a marked parent, if it has one, and every one of its fields has a transferable type. Booleans, integers, floating-point types, text, and bytes are transferable. An object type is transferable only when its layout is marked. Arrays and lists are never transferable, and neither is void. The compiler marks the layouts of transferable classes, payload enums, and tuples, closed under parents.

At a spawn, a send, a select send arm, and a parallel item, the scheduler checks the actual layout of every object reachable from the transferred value through written fields: each must be marked, or the operation is an invalid task operation. A field that has not been written is skipped; reading it later traps as an uninitialized field, as it would without tasks. An image with a `TRNS` section, a scheduler exit, or a poll cannot carry `CALB`, `NATV`, or `neura.ffi.*` imports. An optional raw `TRNS` tag in a pre-1.5 image stays opaque.

### Assembly and decompilation

The assembly form of an exit is `exit scheduler NAME target T arguments [ VALUE... ] result SLOT|- resume B assign [ ... ]`, where `NAME` is the assembly name of the table above. A poll is written between the exit kind and its fields:

```text
exit scheduler scope-enter target 0 arguments [ ] result - resume 1 assign [ ]
exit call poll target 2 arguments [ constant:0:44 constant:2:31 ] result 2 resume 2 assign [ ]
exit scheduler spawn target 4 arguments [ slot:13:4 ] result 5 resume 6 assign [ ]
exit scheduler scope-exit target 0 arguments [ ] result - resume 15 assign [ ]
```

The `TRNS` table is written as `transferable-layouts [ 0 1 2 ]` after the last function. The assembler rejects a poll on any exit but continue and call, and a scheduler exit or poll in an image that declares a version before 1.5. Assembling the disassembly of a valid image reproduces its bytes.

`neura decompile` labels every scheduler operation and every poll `[decoded]`, because the exit record names it and nothing is inferred. It pairs a `scope-enter` with its `scope-exit` when every path to the exit agrees on the open scopes, and marks the pair `[reconstructed: scope bN.eM]`; otherwise it shows `[scope pair unresolved]`. A block whose graph computes nothing is `[decoded]`; a block that has a scheduler exit and a graph that computes values is shown as its circuit, because the effect of the exit is outside the graph model ([Decompiler](../Decompiler.md#tasks-and-scheduler-operations)).

## State operations

The state-operation exit provides checked storage primitives. Its immediate is zero when the table does not assign it a meaning. Reference operands must be live and compatible with the operation. Collection indices and lengths are unsigned 64-bit values at this boundary. A lowering of a signed index must check negative inputs before converting them.

| Code | Operation | Immediate | Arguments | Result |
| --- | --- | --- | --- | --- |
| 0 | Allocate object | Layout ID | None | Reference of the allocated layout |
| 1 | Read type tag | Zero | Object reference | Unsigned 32-bit layout ID |
| 2 | Read field | Field index in the operand's static layout | Object reference | Field value |
| 3 | Write field | Field index in the operand's static layout | Object reference, field value | Void |
| 4 | Free object | Zero | Object reference | Void |
| 5 | Read global | Global index | None | Global value |
| 6 | Write global | Global index | Global value | Void |
| 7 | Allocate collection | Array or list type ID | Initial elements in order | Reference of the given type |
| 8 | Read length | Zero | Array, list, text, or bytes reference | Unsigned 64-bit count |
| 9 | Read element | Zero | Array, list, text, or bytes reference, index | Element value, or unsigned 8-bit byte for text and bytes |
| 10 | Write element | Zero | Array or list reference, index, element value | Void |
| 11 | Append element | Zero | List reference, element value | Void |
| 12 | Remove element | Zero | List reference, index | Void |
| 13 | Construct text or bytes | Text or bytes type ID | Array or list of unsigned 8-bit values | Immutable copy of the selected type |
| 14 (1.1) | Read version | Zero | List reference | Unsigned 64-bit structural version |
| 15 (1.2) | Slice text | Zero | Text, start and end byte offsets | UTF-8 text for the checked half-open range |
| 16 (1.2) | Decode UTF-8 | Layout ID with exactly a Boolean and a text field | Array or list of unsigned 8-bit values | Reference to a pair of validity and decoded text |
| 17 (1.2) | Compare text | Zero | Two text references | Bytewise equality as a Boolean |
| 18 (1.2) | Class hash | Zero | Live object reference | Unsigned 32-bit identity hash |
| 19 (1.2) | Class text | Zero | Live object reference, qualified class name as text | Name and identity number separated by `#` |
| 20 (1.2) | Class test | Target layout ID | Object reference | Boolean, false for nil |
| 21 (1.2) | Class cast | Target layout ID | Object reference | Same handle at the target type, preserving nil |
| 22 (1.2) | Scalar text | Zero | Boolean, integer or float, optionally an unsigned 32-bit ordinal followed by text variants | Deterministically formatted text or the variant name |
| 23 (1.3) | Allocate repeated collection | Array or list type ID | Unsigned 64-bit length and one assignable element | New collection with that length and value in every slot |
| 24 (1.6) | Kernel | `KERN` index | One value per parameter of the kernel record, each assignable to it | The record's result type ([Kernels](#kernels-16)) |

Text operations address UTF-8 bytes at the VM boundary. Constructing text checks UTF-8 validity before publishing the result. Operation 13 accepts a byte list, which can grow as text is assembled. Language text indexing can have its own rules implemented above these byte operations. Arrays have fixed length after allocation. List removal shifts later elements left. Every list carries a structural version that starts at zero when the list is allocated and grows by one, modulo 2^64, with each successful append or removal; writing an element does not change it. A for-each loop compares versions to detect modification. Reads and writes check indices and types before accessing storage. Allocation and growth check size arithmetic and run limits before committing. Operation 23 checks the requested length against storage and heap limits before allocating. Freeing an object invalidates its handle after language cleanup has completed.

### Kernels (1.6)

A kernel state operation calls a built-in C kernel of the runtime ([Kernels](Kernels.md)). Its immediate is an index into the required `KERN` section, a vector of records `name:text, abi_version:u32, parameter_types:vector<u32>, result_type:u32`: the kernel's name and ABI version, and the signature that the image's kernel exits use, in image types. The pair of name and ABI version is unique. The section is present exactly when the image has a kernel exit, and only in a 1.6 image; before 1.6 an optional `KERN` tag is opaque. The writer emits 1.6 only for an image with a kernel exit, so an image without one keeps its earlier version and bytes.

The loader checks each record against the kernel table compiled into the runtime. A name or ABI version the table lacks, or a kernel the runtime cannot bind, makes the image *unsupported*: it is well formed, but this runtime cannot run it, as for an unknown required section or format version. A signature other than the one the kernel's parameter and result kinds give ([Binding a kernel](Kernels.md#binding-a-kernel)) makes the image *invalid*. For each kernel exit the loader checks that the immediate names a record, that the exit has one argument per parameter, each assignable to the parameter's type, and that the result slot is absent for a void result and otherwise accepts the result type.

A kernel exit is a state operation, not a host request: it names no import and needs no capability, and the NVM runs it as [NVM](NVM.md#kernel-calls) describes. The assembly form of the table is one line per record after the imports, `kernel I "NAME" version V parameters [ T... ] result T`, and the exit is `exit state kernel immediate I arguments [ ... ] result S resume B assign [ ... ]`. The assembler rejects either in an image that declares a version before 1.6. `neura decompile` shows a kernel exit as `kernel "NAME" vV(arguments) [opaque kernel]`: the record names the operation, and what it computes is not in the circuit ([Decompiler](../Decompiler.md)).

## Imports and optional source maps

An import record contains `name:text, interface_version:u32, parameter_types:vector<u32>, success_type:u32, error_type:u32`. The tuple of name and interface version is unique within an image. Import names are matched byte for byte. A runner rejects a missing import, an unavailable capability, or a signature mismatch before entry. The standard stdout import is defined in the [NVM specification](NVM.md#host-calls-and-standard-output).

A callback record contains `name:text, interface_version:u32, function:u32`. A callback is a function the runner may call while a host request is in progress, named like an import; [NVM](NVM.md#callbacks) defines the protocol. The tuple of name and version is unique, the function exists and is not the entry function, and its parameters and result use host data. `CALB` is required for execution when present: a runner that does not know it rejects the image instead of running without its callbacks.

The `NATV` section contains `triple:text, object:bytes, symbols:vector<text>`: the target triple the shared object was linked for, the object's bytes, and the symbols the image's C imports use, unique and in ascending byte order. A runner loads the object only when the program first asks for the embedded library, and only on a matching target; on any other target that request fails and the rest of the image runs. The object is native code: the decompiler shows it as an opaque region with its triple, size, and symbols ([Foreign interop](../lang/ForeignInterop.md)).

A source map record contains `function:u32, block:u32, path:text, line:u32, column:u32`. Lines and columns start at one. Entries are sorted by function and block and are unique for each block. Paths are display labels. NVM does not open them. Source maps can be omitted without affecting execution.

### Names

The optional `NAME` section gives tools the source names of image entities. It is provenance only: a runtime ignores it, the loader keeps it as raw bytes without interpreting it, and no tool may infer behavior from it. `neura build` writes it together with the source map and `--no-source-map` omits both. It contains a `u32` count and that many records `kind:u8, index:u32, sub:u32, name:text`:

| Kind | Entity | `index` | `sub` |
| --- | --- | --- | --- |
| 0 | Function | Function | Zero |
| 1 | Layout | Layout | Zero |
| 2 | Field | Layout | Field index |
| 3 | Global | Global | Zero |
| 4 | Frame slot | Function | Slot |

Names are qualified source names, such as `Objects.Counter.bump`, or descriptions of generated functions, such as `Counter.init` and `string +`.

### Lift hints

The optional `LIFT` section records, for blocks whose graphs have neurons, the typed operations the converter built each graph from ([Execution tiers](NVM.md#execution-tiers)). It is a hint, never evidence. The loader keeps it as raw bytes without interpreting it, the section raises no format version, and a loader that does not know it skips it. A runtime that uses it proves every hinted form exactly as it proves a form it found itself: regenerating the block from the form must reproduce the block and its graph, and an overflow analysis of the graph must show that reference evaluation cannot overflow and that every output lane is 0 or 1. A wrong, stale, or malicious hint can only leave its block on the reference evaluator. Execution never depends on the section, the decompiler does not read it, and `neura build --no-lift-hints` omits it; so does `neura build --native --no-lift-hints` of an image already built.

All integers are little-endian. The section contains `version:u32` (1), `form_count:u32`, that many forms, `entry_count:u32`, and that many entries `function:u32, block:u32, form:u32` in strictly ascending (function, block) order, where `form` indexes the forms. Blocks with identical forms share one. A form is:

```text
node_count:u32, node[node_count]
flags:u8                      bit 0: shared; bit 1: has a condition; other bits zero
schedule_count:u32, node:u32[schedule_count]
value_count:u32, (lane:u32, node:u32)[value_count]
condition:u32, exit_when_true:u32      only when flag bit 1 is set
dispatch_count:u32, (exit:u32, layout_count:u32, layout:u32[layout_count])[dispatch_count]
```

Each node is `kind:u8, type:u32` followed by its fields: kind 0, a block input, `input:u32` (its position in the block's input list); kind 1, an immediate, `bits:u64` (its slot encoding); kind 2, a unary operation, `op:u8, operand:u32`; kind 3, a binary operation, `op:u8, left:u32, right:u32`; kind 4, a conversion to `type`, `operand:u32`. An operand names an earlier node, so a form is acyclic, and nesting is at most 256 deep. Operator codes 0 through 24 are `!`, `~`, `index`, `neg`, `+`, `+~`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `&`, `|`, `^`, `<<`, `>>`, `-~`, and `hash`, with the meanings of [Numbers](../lang/Numbers.md) for the operand types; `index` widens a signed or unsigned integer to `uint64` after checking that it is not negative, and `+~` and `-~` add and subtract integers without an overflow check, keeping the result modulo 2^width, as the converter's generated hash loops do. `hash` is the `uint32` value hash of a Boolean, integer, or float operand that `hash_code` returns: a Boolean hashes to 1231 when true and 1237 when false; a float has any NaN replaced by its format's canonical quiet NaN first; a 64-bit value is folded to the exclusive or of its two halves; and a narrower value is extended to 32 bits, with its sign when it is a signed integer. Codes 23 and 24 were added after Neura 0.2.0, whose runtime reads a section that uses either as malformed and ignores it whole, as the next paragraph says; such an image runs there with every block on the reference evaluator or found by search.

A value names the first output lane an exit reads and the node that computes it, in the order the block computes them. In a shared form, a node stands for one computation however often it is used, and the schedule lists the operations in the order the block performs them, including any whose only effect is a check, which fixes the order in which checks fail. The condition chooses between a block's two normal exits: `exit_when_true` is the one taken when it holds. A dispatch form instead lists, for each call exit of a virtual dispatch block, the layouts that select it. A section of another version, or one with any malformed byte (a count beyond the bytes left, an operand that is not an earlier node, an unknown kind or operator, a duplicate lane or exit, entries out of order, or trailing bytes), is ignored whole; an entry that names a function or block outside the image is dropped.

## Writer and loader requirements

The writer emits a stable section order and assigns table indices deterministically for identical build inputs, options, and target versions. It derives layout, function, and block order from deterministic lowering order. It writes no timestamps, padding bytes, or host addresses. Executable sections carry no build-machine paths. Debug paths use an explicit build policy. Unknown data, unsupported source operations, and values outside format limits produce a build error.

The loader bounds counts and byte ranges before allocating or decoding. It rejects truncated records, invalid enum tags, invalid UTF-8, duplicate directory tags, overflowing offset arithmetic, graph cycles, bad references, and ill-typed input and exit bindings. It validates layouts and inherited prefixes, continuation targets, the entry signature, and the shape of every state operation. Runtime checks cover data-dependent failures such as an invalid exit selector, uninitialized values, overflow during graph evaluation, bounds errors, and invalid handles. Invalid images fail before any host import is invoked.

A conformance suite includes executable byte fixtures and rejected images, including truncations and incompatible profiles. The graph evaluator's tests cover output encoding and checked arithmetic. Execution tests cover both branches of conditional effects, call and return continuations, independent object instances, and captured stdout. Determinism tests compare repeated image builds and repeated execution against the same host trace.

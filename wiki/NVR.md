
A callback record contains `name:text, interface_version:u32, function:u32`. A callback is a function the runner may call while a host request is in progress, named like an import; [NVM](https://github.com/Kayyo321/neura/wiki/NVM#callbacks) defines the protocol. The tuple of name and version is unique, the function exists and is not the entry function, and its parameters and result use host data. `CALB` is required for execution when present: a runner that does not know it rejects the image instead of running without its callbacks.

The `NATV` section contains `triple:text, object:bytes, symbols:vector<text>`: the target triple the shared object was linked for, the object's bytes, and the symbols the image's C imports use, unique and in ascending byte order. A runner loads the object only when the program first asks for the embedded library, and only on a matching target; on any other target that request fails and the rest of the image runs. The object is native code: the decompiler shows it as an opaque region with its triple, size, and symbols ([Foreign interop](https://github.com/Kayyo321/neura/wiki/ForeignInterop)).
# Neura Virtual Runtime image format

An NVR image is a binary executable for the [Neura Virtual Machine](NVM). The file extension is `.nvr`. The image is the output of `IRtoNeuronConversion` and serialization. Its executable records contain everything needed to evaluate the compiled program with a compatible NVM and its declared host services.

## Encoding and compatibility

Version 1 uses fixed-width little-endian integers. Signed integers use two's-complement encoding. There is no implicit padding. `u8`, `u16`, `u32`, and `u64` denote unsigned integers of those widths. `i64` denotes a signed 64-bit integer. A `vector<T>` is a `u32` element count followed by that many consecutive records. `text` is a byte vector containing valid UTF-8. `bytes` is a byte vector with arbitrary contents. Record fields appear in the order listed.

A reference into a table is its zero-based `u32` index. `0xffffffff` marks absence only in fields declared optional. Graph wires, blocks, exits, fields, and frame slots have their own local index spaces. Table and local counts must leave the reserved index available. The format version governs binary encoding. The graph profile governs evaluation semantics. Host service versions govern the contracts for external operations. These version axes are independent.

### File header

| Field | Type | Value |
| --- | --- | --- |
| `CALB` | Callback records (1.4); present only when the image has callbacks | Yes, when present |
| Magic | 4 bytes | ASCII `NEUR` |
| Major | u16 | 1 |
| Minor | u16 | 0 or 1 |
| Graph profile | u32 | 1 for checked integer identity and ReLU graphs |
| Section count | u32 | Number of directory records |
| File size | u64 | Total size in bytes, including the header and directory |

The section directory immediately follows the header. Each directory record is `tag[4], flags:u32, offset:u64, size:u64`. Tags use four printable ASCII characters and compare bytewise. Offsets are absolute byte offsets. Bit zero of flags means the section is required for execution. Other flag bits are reserved and zero in version 1.0. Sections follow the directory in ascending tag order, without gaps or overlaps. The directory uses the same order and contains each tag at most once.

### Versions 1.0 through 1.4

Minor version 1 adds float and bfloat type kinds (tags 9 and 10), the read-version state operation (code 14), and trap reason 5. Minor version 2 adds an interface vector to each layout record, a nil object constant, a message-trap exit, and state operations 15 through 22. Minor version 3 adds the repeated collection allocation state operation (code 23). Minor version 4 adds the `CALB` callback table and the `NATV` embedded native library. The graph profile remains 1.

- A writer emits the lowest minor version whose features the image uses. A writer that re-encodes a loaded image keeps at least the minor version the image declared.
- The loader accepts major version 1 with minor versions 0 through 4. A feature newer than the version declared by the image makes the image malformed.
- An unknown required section or graph profile produces an unsupported-image error. A recognized version can carry unknown optional sections. The loader keeps their bytes so that re-encoding the image reproduces them; execution ignores them. A known section must carry the required flag given in the table below. Executable sections never reference optional sections.

| Tag | Contents | Required |
| --- | --- | --- |
| `CONS` | Constant records | Yes |
| `FUNC` | Function, frame, block, and exit records | Yes |
| `GLOB` | Global slot declarations | Yes |
| `GRPH` | Neuron graph records | Yes |
| `IMPT` | Host import declarations | Yes |
| `LAYO` | Object layout records | Yes |
| `META` | Entry function index as one `u32` | Yes |
| `NATV` | The embedded native library (1.4); present only when the image has C imports | Yes, when present |
| `NAME` | Source names for tools (provenance; not interpreted by the loader) | Optional |
| `SMAP` | Source locations for function blocks | Optional |
| `TYPE` | Type records | Yes |

Every section except `META` stores a vector of records. A required table with zero entries still carries a zero count. Readers must consume every byte in each recognized section.

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

Void is valid as a result type and has no slot, constant, or value descriptor. Signedness and width are part of the type. Assignments and calls require structural type equality, or a declared object subtype where a base type is expected. Arrays and lists are invariant in their element type. Object subtyping follows parent and interface links. Profile 1 uses bit vectors at graph ports, including for opaque handles. A float value's lanes are its IEEE encoding, least significant bit first, so graphs compute float arithmetic bit by bit ([Numbers](Numbers)).

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

Resume blocks belong to the current function. For every exit that produces a result (a call, a state operation, and both continuations of a host call), a void result requires an absent slot, and a nonvoid result requires a slot to which the result is assignable. Return values and arguments must be assignable to their declared types. A trap exit has no assignments. Reason codes 1 through 4 designate source arithmetic failure, source bounds failure, explicit program failure, and invalid dispatch. Reason 5 (1.1) designates a list modified during for-each. Other codes require a format revision. Loader and runtime faults have separate diagnostics.

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

Text operations address UTF-8 bytes at the VM boundary. Constructing text checks UTF-8 validity before publishing the result. Operation 13 accepts a byte list, which can grow as text is assembled. Language text indexing can have its own rules implemented above these byte operations. Arrays have fixed length after allocation. List removal shifts later elements left. Every list carries a structural version that starts at zero when the list is allocated and grows by one, modulo 2^64, with each successful append or removal; writing an element does not change it. A for-each loop compares versions to detect modification. Reads and writes check indices and types before accessing storage. Allocation and growth check size arithmetic and run limits before committing. Operation 23 checks the requested length against storage and heap limits before allocating. Freeing an object invalidates its handle after language cleanup has completed.

## Imports and optional source maps

An import record contains `name:text, interface_version:u32, parameter_types:vector<u32>, success_type:u32, error_type:u32`. The tuple of name and interface version is unique within an image. Import names are matched byte for byte. A runner rejects a missing import, an unavailable capability, or a signature mismatch before entry. The standard stdout import is defined in the [NVM specification](NVM#host-calls-and-standard-output).

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

## Writer and loader requirements

The writer emits a stable section order and assigns table indices deterministically for identical build inputs, options, and target versions. It derives layout, function, and block order from deterministic lowering order. It writes no timestamps, padding bytes, or host addresses. Executable sections carry no build-machine paths. Debug paths use an explicit build policy. Unknown data, unsupported source operations, and values outside format limits produce a build error.

The loader bounds counts and byte ranges before allocating or decoding. It rejects truncated records, invalid enum tags, invalid UTF-8, duplicate directory tags, overflowing offset arithmetic, graph cycles, bad references, and ill-typed input and exit bindings. It validates layouts and inherited prefixes, continuation targets, the entry signature, and the shape of every state operation. Runtime checks cover data-dependent failures such as an invalid exit selector, uninitialized values, overflow during graph evaluation, bounds errors, and invalid handles. Invalid images fail before any host import is invoked.

A conformance suite includes executable byte fixtures and rejected images, including truncations and incompatible profiles. The graph evaluator's tests cover output encoding and checked arithmetic. Execution tests cover both branches of conditional effects, call and return continuations, independent object instances, and captured stdout. Determinism tests compare repeated image builds and repeated execution against the same host trace.

# Neura Virtual Machine

The Neura Virtual Machine (NVM) loads and executes a Neura Virtual Runtime image (`.nvr`). An image contains compiled neuron graphs, typed execution metadata, constants, and the host imports needed by the program. This specification defines the NVM execution contract. [NVR format](NVR.md) defines its binary encoding.

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

At the graph boundary, each value occupies a sequence of bit lanes. Lane zero is the least significant bit. Booleans occupy one lane, and integers occupy their declared width with two's-complement encoding for signed values. References cross the graph boundary as 64-bit opaque handles. Every output lane consumed by the selected exit, including the selector, must be exactly zero or one. A selector outside the block's exit table traps. Source arithmetic and comparisons are expressed by the graph, including checks and exits for specified arithmetic failures. Checked neuron arithmetic is an execution rule for the target and is distinct from the source language's arithmetic rules.

A typed value descriptor can read a frame slot, a constant, or an output lane slice. The runtime transports opaque handles and checks their type and validity when used. Language-level equality, arithmetic, and control decisions are represented in the circuit. Execution metadata can transport a constant or a live reference without routing it through a neuron.

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

## Heap and objects

Objects have a runtime type tag, a layout ID, and typed field slots. Layouts preserve inherited fields at stable indices. A method receives the object handle as an explicit argument. Each object has independent storage, while calls share the compiled method graphs. Collections use typed element slots, and lengths and indices are checked before accessing storage.

An allocation creates a fresh handle and uninitialized fields. Compiler-generated constructor control flow performs base initialization, field initialization, and the declared constructor body in language order. It exposes the result of `new` on successful completion. If the language permits a field to be initialized later, its slot remains uninitialized until a store. A premature load traps. Field and collection stores validate the handle, layout, index, and value type before changing storage. Compiler-generated destruction calls the required cleanup methods and then invalidates the handle. Freed handles cannot name later allocations. These rules define runtime checks. The language specification defines which source operations are legal and the ordering of language-visible initialization and cleanup.

For example, incrementing a counter runs a block that requests a field read, a graph that adds to the returned value, and a block that requests a field write. The heap handler performs the read and write. The graph supplies the new value.

## Host calls and standard output

A host import declares a service name, interface version, typed arguments, and typed success and error results. An exit references an import by table index. At load time the runtime binds each declared import to a registered service with the same signature and version under the runner's service policy. Imports grant access only to their bound service. A service that needs filesystem or network access defines that authority in its own interface and configuration.

A request contains the import, resolved arguments, and an execution-local sequence number. NVM issues the request once on entering the selected exit. A completion supplies either the success or error value. The runtime type-checks that value, writes the designated result slot, and enters the corresponding resume block. A suspended run keeps the pending request and resumes from its completion. The execution contract covers a live run. Durable recovery across a process crash requires a separate persistence protocol.

The standard console service is `neura.console.write_line` version 1. It takes a UTF-8 string and returns either success with no payload or an unsigned 32-bit error code. Error code 1 denotes a failed write. It emits the string's bytes followed by a single LF byte, independent of platform newline conventions. A host write can partially succeed before reporting failure. The runtime reports the failure without retrying. The language library determines how that result appears to source code.

For `io.println("Hello, World!")`, the image stores the string in its constant pool. The entry graph selects a host-call exit whose argument names that constant. The bound console handler writes the line and NVM resumes at the exit's success block. The entry function returns a signed 32-bit status. The launcher maps that status to the platform process result. A trap yields a diagnostic and a nonzero process result. The embedding API exposes the full status or structured trap independently of the platform's exit-code range.

## Determinism, limits, and observability

Given the same validated image, initial inputs, resource limits, and host completion trace, a run yields the same exit selections, effects, state updates, and terminal outcome. Clock readings, file contents, and similar external responses enter through the host boundary. Tests can supply those responses from a trace and verify the emitted request sequence. The reference executor runs one block at a time and waits for each selected host call before resuming. A faster executor must preserve that order where it is observable.

The loader applies size and count limits before allocating image structures. The runner enforces declared bounds on frame depth, live heap storage, and execution steps. Exhaustion traps at the operation that exceeds the configured limit. Limit checks occur before the affected allocation or state mutation. A trap stops further program execution and leaves any completed external effects visible.

Diagnostics identify the runtime fault and, when execution has begun, the function and block. Optional source maps add source locations. A trace can expose block entries, graph exits, and requests and responses. Inspection and decompilation use the executable graph and continuation tables, with source maps as supplemental information.

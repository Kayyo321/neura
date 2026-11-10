# GPU execution of neuron graphs: implementation plan

This page is a plan. Nothing in it is implemented. It specifies how `neura run` and `neura test` evaluate neuron graphs on a GPU by default, with `--no-gpu` as the opt-out, and how that is built and verified. The [NVM specification](../runtime/NVM.md) and [NVR format](../runtime/NVR.md) remain the contracts for execution; this plan adds a second way to carry out one step of the NVM contract and changes neither document's semantics.

## 1. How to read this plan

- "Must" states a requirement on the implementation. A constant, name, message, or layout given here is fixed. An implementer who finds one unworkable stops and reports it; they do not pick a replacement.
- Section 3 lists every decision. Section 4 gives the measurements those decisions rest on, and says which are measurements and which are estimates.
- The **reference evaluator** is `nvm::evaluate_graph` in `src/nvm/machine.cpp`. It defines the result of evaluating a graph. Every other evaluator in this plan is correct only where it returns exactly what the reference evaluator returns.
- A **request** is one evaluation of one graph on one set of input lanes. A **dispatch** is one submission to the GPU carrying one or more requests.

**Stages affected.** Only the runtime after the output stage: the NVM library, a new GPU library, and the CLI. The lexer, parser, NeuraIR, IR optimization, IRtoNeuronConversion, the NVR writer and loader, and the decompiler do not change. The image format, the graph profile, and every image byte stay the same.

## 2. Summary

Each NVM step resolves a block's inputs, evaluates the block's graph, decodes the selected exit, and commits it ([block transition](../runtime/NVM.md#execution-state)). This plan moves the second action, graph evaluation, to a Vulkan compute device. Everything else stays in the CPU NVM: input resolution, the selector and lane checks, exits, frames, the heap, garbage collection, limits, host calls, traces, and every trap diagnostic.

The GPU evaluates a graph layer by layer in exact 32-bit integer arithmetic. A static check on each graph's weights and a run-time bound on every neuron's value together guarantee that the 32-bit result equals the reference evaluator's checked 64-bit result; a request that leaves the bound is recomputed by the reference evaluator. No floating-point operation is involved, so results do not depend on the device.

A run with the GPU and a run with `--no-gpu` must be indistinguishable to the program and to anyone reading its output, status, trap diagnostics, step counts, heap figures, and host traces.

## 3. Decisions

| ID | Decision |
| --- | --- |
| D1 | `neura run` and `neura test` evaluate graphs on a GPU by default. `--no-gpu` selects the reference evaluator. This is the owner's direction and is not conditional on any measurement in this plan. |
| D2 | The GPU performs graph evaluation only. Frames, slots, exits, state operations, the heap, collection, limits, host requests, and trap construction stay in `Execution::State`. |
| D3 | No NVR, NIR, or graph profile change. Profile 1 keeps its definition: checked signed 64-bit arithmetic, ascending-source edge order, stored node order. |
| D4 | The device API is Vulkan 1.0 compute with no extensions and no layers. The Vulkan loader is opened at run time; the build needs only Vulkan headers and `glslangValidator`. |
| D5 | GPU arithmetic is 32-bit two's-complement integer. Exactness comes from the weight budget and the wire bound of section 6, with `wire_bound = 32767`. |
| D6 | The kernel is the layered kernel of section 8: one work group per request, 64 invocations per work group, one barrier per layer. Its source in section 8 is normative. |
| D7 | Every eligible graph with at least one neuron goes to the device. There is no cost model, size threshold, sampling, or timing-based placement. A graph with no neurons is never sent, because there is nothing to evaluate. |
| D8 | When no usable device exists, the run uses the reference evaluator and prints nothing. `--require-gpu` turns that case into an error before entry. |
| D9 | The NVM library's default stays the reference evaluator. `nvm::Execution`, `nvm::run`, `nvm::record`, and `nvm::replay` use a device only when the caller passes one. Only the CLI applies D1. |
| D10 | Device choice is deterministic: discrete, then integrated, then virtual, then other, each in loader enumeration order. Software implementations (`VK_PHYSICAL_DEVICE_TYPE_CPU`) are used only with `--gpu-software`. |
| D11 | The existing CTest entries run with `NEURA_GPU=off`, so their evidence continues to come from the reference evaluator and their duration does not depend on the machine. New entries cover the GPU path. |
| D12 | macOS is outside this plan. `NEURA_GPU=AUTO` resolves to `OFF` on Apple platforms. The Windows path is specified and unverified, as MSVC builds are today ([Backend](../Backend.md#tests-and-the-limits-of-the-evidence)). |

**Reversing D1.** If the owner later wants the GPU to be opt-in, only section 12 changes: the default becomes the reference evaluator, a new `--gpu` option selects the behavior this plan gives to "no option", and `--no-gpu` remains accepted. Nothing else in the plan depends on which way the default points.

## 4. Measurements behind the design

These are observations on one machine on 2026-11-10, with a throwaway prototype that is not in the repository. They are not guarantees for other hardware. The prototype used the kernel source and buffer layouts of sections 7 and 8 exactly, and compared every output with a Python model of the reference rule.

**Machine.** AMD Ryzen 7 9800X3D; NVIDIA GeForce RTX 5080 (proprietary driver, Vulkan 1.4.357); AMD Radeon integrated graphics (RADV, Vulkan 1.4.305); llvmpipe software Vulkan (LLVM 19.1.7). `build/neura` was a Release build of the working tree.

**Graph shapes.** Images built from `tests/benchmarks/sha256.nra`, `sort.nra`, `time-text.nra`, and the hello-world example hold 16 to 114 graphs each. Across them:

| Property | Observed |
| --- | --- |
| Largest graph | 20,735 neurons, 90,054 edges, 128 input lanes, 4,290 layers |
| Neurons per layer, mean | Among each image's 14 largest graphs: 1.0 to 14, with one graph at 71; 4.4 to 9.0 for the four largest graphs |
| Widest layer | 221 neurons |
| Largest magnitude of a weight / a bias | 64 / 64 |
| Largest fan-in / largest sum of weight magnitudes at one neuron | 192 / 256 |
| Largest neuron value, on random inputs to each image's 14 largest graphs | 29 |
| Share of an image's graphs with at most 256 neurons | 80 to 100 percent |

Compiled graphs are deep and narrow. A layer rarely holds more than a handful of neurons, so one graph offers little work to do in parallel.

**Interval analysis does not prove the bound.** A static interval analysis that propagates `[low, high]` per wire proves a 31-bit bound for most graphs and fails for 3 of 80, 3 of 66, and 17 of 114 graphs in the three benchmark images, the largest ones, with proven magnitudes of up to 1,043 bits. The likely cause, which was not verified, is that intervals lose the relation between the two neurons of a threshold pair and the loss compounds through each layer. Section 6 therefore uses a static weight budget with a run-time bound instead.

**CPU baseline.** `neura run --stats` wall time, including process start: sha256 at 10,240 bytes, 163,797 steps in 0.19 s; sort at 20,000 elements, 4,839,986 steps in 1.03 s; hash-map at 20,000, 4,642,080 steps in 0.61 s. That is 0.13 to 1.2 microseconds per step. Hello world takes 26 steps and about 1 ms.

**One request per dispatch.** Median round trip in microseconds: write inputs, submit, wait for the fence by polling, read outputs. The CPU column is a C++ loop with the reference rule's checked 64-bit arithmetic.

| Graph (neurons / edges / layers) | CPU | RTX 5080 | Radeon integrated | llvmpipe |
| --- | --- | --- | --- | --- |
| 24 / 47 / 24 | 0.08 | 24.7 | 40.3 | 25.7 |
| 193 / 888 / 66 | 0.58 | 100.3 | 112.8 | 47.6 |
| 499 / 1,373 / 7 | 1.05 | 23.5 | 40.1 | 22.2 |
| 3,610 / 14,087 / 393 | 9.53 | 911.3 | 1,020.0 | 292.2 |
| 20,735 / 90,054 / 4,290 | 56.5 | 6,479.2 | 6,634.0 | 2,410.6 |

A single request costs about 20 to 40 microseconds of fixed round trip plus 1.1 to 2.3 microseconds per layer on the two GPUs, about 1.5 for the deepest graph. It is 22 to 309 times slower than the CPU on the RTX 5080.

**Many requests per dispatch.** Microseconds per request, with 4,096 requests per dispatch for the first three graphs and 256 for the last two:

| Graph | CPU | RTX 5080 | Radeon integrated | llvmpipe |
| --- | --- | --- | --- | --- |
| 24 neurons | 0.08 | 0.020 | 0.340 | 2.53 |
| 193 neurons | 0.58 | 0.087 | 2.62 | 7.27 |
| 499 neurons | 1.05 | 0.039 | 0.385 | 2.42 |
| 3,610 neurons | 9.53 | 3.56 | 26.6 | 57.5 |
| 20,735 neurons | 56.5 | 25.3 | 162.9 | 469.8 |

Batched, the discrete GPU is 2.2 to 27 times faster per request than the CPU. The integrated GPU is slower than the CPU for four of the five graphs, and llvmpipe is slower for all of them.

**Start-up.** Creating the Vulkan instance, device, and pipeline and running six dispatches took 91 to 132 ms per process on the RTX 5080 and about 40 ms on the other two devices.

**Correctness of the prototype.** Zero output mismatches on all three devices for every graph above, for a mixed dispatch of 5,120 requests over five different graphs, and for a synthetic graph that leaves the wire bound, where all 512 expected escapes and no others were reported. This is an observed match on sampled random inputs, not an exhaustive check or a proof.

**What follows from this.**

1. The barrier-per-layer kernel works on three independent Vulkan implementations.
2. `wire_bound = 32767` leaves three orders of magnitude of room above the largest observed neuron value, and the weight budget of section 6 holds for every sampled graph with room to spare (64 + 32767 × 256 is under 2²⁴).
3. With one execution and one graph per step, GPU evaluation is slower than the reference evaluator. Section 16 states what users should expect and what would change it. D1 stands regardless.
4. A software Vulkan device never helps, which is why D10 excludes it unless asked for.

## 5. Contract

### 5.1 What must be identical

For the same image, arguments, limits, and host completions, a run with a graph device and a run without one must agree on:

- every exit selection, slot assignment, state operation, and host request, in order, with the same arguments;
- the terminal outcome: status, or the trap's kind, reason, detail text, function, block, and caller list;
- `steps`, `peak_heap_cells`, `function_steps`, and the host request count;
- standard output and the trap diagnostic on standard error;
- a trace written by `--record`, byte for byte, and the result of `--replay`.

This follows from the device contract in section 9.1, because the device replaces only the computation of the wire values that `read` and `select` consume.

### 5.2 What may differ

- Wall time and process start-up time.
- The `gpu:` line that `--stats` adds (section 12.5).
- Runner errors that exist only for the GPU options (section 12.4).

### 5.3 What stays in the circuit and what crosses the boundary

The circuit is unchanged: the same graphs, weights, biases, and activations decide every arithmetic result and every exit. The GPU is not a host service. It has no import, no capability, no entry in a host trace, and the program cannot observe it. It is an internal implementation choice of the kind [NVM](../runtime/NVM.md#execution-state) permits when observable behavior is identical. Data that crosses to the device per request: the graph's index, its input lanes as zeros and ones, and back, the values of its output wires and one status word. Reference handles cross only as the 64 bit lanes a graph already receives; the device never dereferences them.

## 6. Eligibility and exactness

### 6.1 Constants

```text
wire_bound      = 32767            largest |value| a wire may hold on the device
sum_limit       = 2147483647       2^31 - 1
local_size      = 64               invocations per work group
```

### 6.2 Static eligibility

A graph is **eligible** when all of the following hold. The check runs once per image, in ascending graph index order.

1. It has at least one node.
2. For every node, `|bias| + wire_bound × Σ|weightᵢ| ≤ sum_limit`, where the sum is over the node's edges. The implementation computes magnitudes as unsigned 64-bit values (the magnitude of `INT64_MIN` is 2⁶³) and treats any unsigned overflow of the product or the sum as failing the test.
3. Its packed records fit the table limit of section 7.4.

Rule 2 implies that the bias and every weight fit in 32 bits. A graph that fails a rule is **ineligible** for the life of the device and is evaluated by the reference evaluator on every step. The failing rule is recorded per graph as `no_nodes`, `weight_budget`, or `table_limit`.

### 6.3 Run-time bound

During a request, a node whose activated value `v` satisfies `v > wire_bound` or `v < -wire_bound` marks the request **escaped**. The device discards an escaped request's outputs and declines it; the caller evaluates it with the reference evaluator. After its first escape a graph is demoted to ineligible with reason `escaped`, so a graph that leaves the bound costs at most one wasted request.

### 6.4 Why the result is exact

Claim: for an eligible graph and a request that is not escaped, every wire value on the device equals the reference evaluator's value, and the reference evaluator reports no overflow.

Induction over layers. Input lanes are 0 or 1, so their magnitude is at most `wire_bound`. Take a node whose source wires are all exact and within `wire_bound`. Each product has magnitude at most `|weightᵢ| × wire_bound`, and the bias plus any subset of the products has magnitude at most `|bias| + wire_bound × Σ|weightᵢ| ≤ sum_limit`. So every product and every partial sum, in any order, is representable in 32 bits, the 32-bit computation is exact, and the same values in the reference evaluator are far inside 64 bits, so it does not overflow at this node. The activation is exact. If the activated value were outside `wire_bound`, the request would be escaped; it is not, so the value is within the bound and the induction continues.

Conversely, if any reference wire value exceeds `wire_bound`, take such a node in the earliest layer. Its sources are exact and bounded, so the device computes its value exactly and marks the request escaped. Therefore no request is wrongly accepted, and no request whose wires all stay in the bound is wrongly escaped.

The order of evaluation differs from the reference evaluator (layers instead of node order, any edge order). With exact arithmetic the order cannot change a value. The only order-dependent behavior of the reference evaluator is which node it reports on overflow, and an overflow can occur only in a request the device declines.

## 7. Packed tables

`src/nvm/layered.hpp` and `layered.cpp` (library `neura_nvm`, namespace `neura::nvm`) build the tables from `image.graphs`. They depend on nothing but the image model, so they are tested on every machine.

### 7.1 Layers

For a graph, wires `0 .. input_lanes - 1` have layer 0. A node's layer is one more than the largest layer among its source wires, and 1 for a node without edges. `layer_count` is the largest node layer. Nodes are sorted by `(layer, node index)`; a node's **position** is its index in that order.

### 7.2 `TABLES`

One array of unsigned 32-bit words. Signed fields store the two's-complement bit pattern. Each eligible graph gets a **slot**, numbered from 0 in ascending graph index order.

Directory: slot `d` occupies words `8d .. 8d + 7`.

| Word | Field | Meaning |
| --- | --- | --- |
| 0 | `input_lanes` | The graph's input lane count |
| 1 | `node_base` | Word index of the node record at position 0 |
| 2 | `edge_base` | Word index of the graph's first edge record |
| 3 | `layer_base` | Word index of the layer array |
| 4 | `layer_count` | Number of layers |
| 5 | `output_base` | Word index of the output wire list |
| 6 | `output_count` | Length of `graph.outputs` |
| 7 | `wire_count` | `input_lanes + node count` |

After the directory, for each slot in order: its node records, its edge records, its layer array, its output list.

- **Node record**, four words at `node_base + 4 × position`: `bias`; `edge_first`, the index of the node's first edge among the graph's edge records; `(edge_count << 1) | relu`, where `relu` is 1 for ReLU and 0 for identity; `wire`, which is `input_lanes + node index`.
- **Edge record**, two words at `edge_base + 2 × edge index`: `source` wire, then `weight`. A node's edges are stored consecutively in the image's ascending source order; nodes' edge runs follow one another in position order.
- **Layer array**, `layer_count + 1` words: word `l` is the position of the first node of layer `l + 1`; the last word is the node count.
- **Output list**, `output_count` words: `graph.outputs` in order, repeats included.

### 7.3 `IO` and `WIRES`

`IO` is an array of unsigned 32-bit words written by the CPU before a dispatch and read by it afterwards. A dispatch of `R` requests uses:

- words `4r .. 4r + 3` for request `r`: `slot`, `wire_base` (word index into `WIRES`), `input_base` and `output_base` (word indices into `IO`);
- for each request in order after the request records, its **input region** of `input_lanes` words, each 0 or 1, then its **output region** of `output_count + 1` words. The CPU sets the last word, the **status word**, to 0 before the dispatch. The device stores output wire values in the first `output_count` words and sets status bit 0 when the request escaped and bit 1 when it finished.

`WIRES` is an array of signed 32-bit words. Request `r` owns `wire_count` words starting at its `wire_base`; requests are laid out consecutively from 0. A request writes every one of its wires before any invocation reads it, so the array is never cleared.

After a dispatch a status word must be 2 (evaluated) or 3 (escaped). Any other value is a device failure (section 9.5).

### 7.4 Limits

| Limit | Value |
| --- | --- |
| `TABLES` size | At most the smaller of 2 GiB and the device's `maxStorageBufferRange`. Graphs are packed in ascending index order; a graph whose records would exceed the limit is ineligible with `table_limit` and packing continues with the next graph. |
| `IO` capacity | Starts at the larger of 1 MiB and four bytes times `5 + max(input_lanes + output_count)` over eligible graphs. Grows to the next power of two that fits a dispatch, up to 256 MiB. |
| `WIRES` capacity | Starts at the larger of 1 MiB and four bytes times the largest eligible `wire_count`. Grows the same way, up to 256 MiB. |
| Requests per dispatch | At most the smaller of 65,535 and the device's `maxComputeWorkGroupCount[0]`. |

A call with more requests than one dispatch can hold is split into consecutive dispatches, each taking the longest prefix of the remaining requests that fits all three limits. The single-request path of `Execution` always fits the initial capacities, so it never reallocates.

## 8. The kernel

`src/gpu/graph_eval.comp`. This source is normative. The `coherent` qualifier on `Wires` and the `memoryBarrierBuffer(); barrier();` pairs are what the shading language specifies for making one invocation's buffer writes visible to the others in its work group. They must not be removed, although an earlier prototype variant measured `coherent` at up to 1.8 times the kernel time on one driver.

```glsl
#version 450
layout(local_size_x = 64) in;
layout(std430, set = 0, binding = 0) readonly buffer Tables { uint tables[]; };
layout(std430, set = 0, binding = 1) buffer Io { uint io[]; };
layout(std430, set = 0, binding = 2) coherent buffer Wires { int wires[]; };
const int WIRE_BOUND = 32767;
void main() {
    uint lid = gl_LocalInvocationID.x;
    uint request = 4u * gl_WorkGroupID.x;
    uint entry = 8u * io[request];
    uint wire_base = io[request + 1u];
    uint input_base = io[request + 2u];
    uint output_base = io[request + 3u];
    uint input_lanes = tables[entry];
    uint node_base = tables[entry + 1u];
    uint edge_base = tables[entry + 2u];
    uint layer_base = tables[entry + 3u];
    uint layer_count = tables[entry + 4u];
    uint output_list = tables[entry + 5u];
    uint output_count = tables[entry + 6u];
    bool escaped = false;
    for (uint j = lid; j < input_lanes; j += 64u) wires[wire_base + j] = int(io[input_base + j]);
    memoryBarrierBuffer();
    barrier();
    for (uint layer = 0u; layer < layer_count; ++layer) {
        uint first = tables[layer_base + layer];
        uint last = tables[layer_base + layer + 1u];
        for (uint p = first + lid; p < last; p += 64u) {
            uint node = node_base + 4u * p;
            int sum = int(tables[node]);
            uint edge = edge_base + 2u * tables[node + 1u];
            uint packed = tables[node + 2u];
            uint edge_end = edge + 2u * (packed >> 1);
            for (; edge < edge_end; edge += 2u) sum += int(tables[edge + 1u]) * wires[wire_base + tables[edge]];
            if ((packed & 1u) != 0u) sum = max(sum, 0);
            escaped = escaped || sum > WIRE_BOUND || sum < -WIRE_BOUND;
            wires[wire_base + tables[node + 3u]] = sum;
        }
        memoryBarrierBuffer();
        barrier();
    }
    for (uint k = lid; k < output_count; k += 64u) io[output_base + k] = uint(wires[wire_base + tables[output_list + k]]);
    uint status = output_base + output_count;
    if (escaped) atomicOr(io[status], 1u);
    if (lid == 0u) atomicOr(io[status], 2u);
}
```

One work group handles one request; `gl_WorkGroupID.x` is the request index. Within a layer, invocation `lid` handles positions `first + lid`, `first + lid + 64`, and so on. The layer loop's trip count is the same for every invocation of a work group, so each `barrier()` is in uniform control flow. Invocations write disjoint wires and disjoint output words; the status word is written only through `atomicOr`.

The dispatch is `vkCmdDispatch(R, 1, 1)` for `R` requests. There are no push constants and no specialization constants.

## 9. NVM integration

### 9.1 `GraphDevice`

New header `src/nvm/graph_device.hpp`, library `neura_nvm`:

```cpp
namespace neura::nvm {

struct GraphJob {
    std::uint32_t graph = 0;                 // index into image.graphs
    std::span<const std::int64_t> inputs;    // input lanes in wire order, each 0 or 1
    std::span<std::int64_t> outputs;         // receives wire graph.outputs[k] at index k
    bool evaluated = false;                  // set by the device
};

struct GraphDeviceStatistics {
    std::uint64_t evaluated = 0;             // jobs returned with evaluated == true
    std::uint64_t declined_ineligible = 0;   // jobs for ineligible graphs
    std::uint64_t escaped = 0;               // jobs that left the wire bound
    std::uint64_t declined_failed = 0;       // jobs declined after a device failure
    std::uint64_t dispatches = 0;
};

class GraphDevice {
public:
    virtual ~GraphDevice() = default;
    virtual const nvr::Image& image() const = 0;
    virtual void evaluate(std::span<GraphJob> jobs) = 0;
    virtual GraphDeviceStatistics statistics() const = 0;
};

}
```

Contract of `evaluate`, for every job:

- If it sets `evaluated` to true, then `outputs[k]` equals the value the reference evaluator assigns to wire `graph.outputs[k]` for those inputs, for every `k`, and the reference evaluator reports no overflow for those inputs.
- Otherwise it sets `evaluated` to false and the contents of `outputs` are unspecified. The caller must then use the reference evaluator.
- A device failure never throws; the device declines the job.
- `graph` out of range, `inputs.size() != input_lanes`, `outputs.size() != graph.outputs.size()`, or an input other than 0 or 1 is a caller bug and throws `std::invalid_argument`.

A device belongs to one image object. It reads `image.graphs` once, when constructed, and must not be used after the image is destroyed or its graph table changes. `neura test` changes `image.entry` between tests; that is allowed.

### 9.2 `RunLimits` and `Execution`

`RunLimits` gains one field, after `profile`:

```cpp
// Evaluates graphs for this run, or nullptr for the reference evaluator.
// The device must have been created for the image the run executes.
GraphDevice* graph_device = nullptr;
```

Both `Execution::State` constructors throw `std::invalid_argument("graph device was created for another image")` when `limits.graph_device` is set and `&limits.graph_device->image() != &image`.

`Execution::State::evaluate` changes as follows. Apart from the constructor check above and the new member vector, nothing else in `machine.cpp` changes:

1. Resolve the block's inputs through `read`, as today. A trap here happens before any device call.
2. Assign `wires_` to `input_lanes + nodes` zeros and write the input lanes, as today.
3. If `limits_.graph_device` is not null and the graph has at least one node: resize a member vector `device_outputs_` to `graph.outputs.size()`, build one `GraphJob{block.graph, {wires_.data(), graph.input_lanes}, device_outputs_}`, and call `evaluate` with that single job. If the job comes back evaluated, store `device_outputs_[k]` into `wires_[graph.outputs[k]]` for every `k` and return.
4. Otherwise call `evaluate_graph(graph, wires_)` and raise `GraphOverflow` exactly as today.

`select` and `read` are unchanged. They read only `wires_[graph.outputs[i]]`, which step 3 has made equal to the reference values, so every later check, message, and trap is produced by the same code on the same values. `steps_`, the step limit, profiling, and the order of faults within a step are untouched.

`nvm::run`, `nvm::record`, and `nvm::replay` already pass `RunLimits` through to `Execution` and need no change.

### 9.3 `ModelDevice`

`src/nvm/layered.hpp` also declares `ModelDevice`, a `GraphDevice` that runs the kernel's algorithm on the CPU from the same `TABLES` words: 32-bit arithmetic, layer order, the escape rule, the demotion rule, and the status values. It processes a layer's positions in ascending order. It exists so that eligibility, packing, and the exactness argument are tested everywhere, including machines without Vulkan. It is not used by the CLI.

### 9.4 `VerifyingDevice`

`src/nvm/graph_device.hpp` declares `VerifyingDevice`, which wraps another device. For every job the inner device evaluates, it runs `evaluate_graph` on a scratch copy of the inputs and compares. On a difference it throws `GraphDeviceMismatch`, a `std::runtime_error`, with one of these messages:

```text
graph device mismatch: graph <g> output <k>: device <value>, reference <value>
graph device mismatch: graph <g>: reference evaluation overflows at node <n>
```

The exception leaves `Execution::advance()`, which catches only traps, and ends the run. `image()` and `statistics()` forward to the inner device.

### 9.5 Device failure

A device **fails** when a Vulkan call returns an error, a dispatch does not complete within 30 seconds, or a status word is neither 2 nor 3. From then on it declines every job, including the rest of the failing call, counts them in `declined_failed`, and keeps a one-line reason. The run continues on the reference evaluator with identical results. A failed device is never retried.

## 10. The Vulkan device

New library `neura_gpu` in `src/gpu/`, namespace `neura::gpu`, linking `neura_nvm`. It is built only when the build enables it (section 13).

| File | Contents |
| --- | --- |
| `src/gpu/vulkan_api.hpp`, `.cpp` | Opens the loader and holds the function pointers. |
| `src/gpu/device.hpp`, `.cpp` | `Context` and `VulkanGraphDevice`. |
| `src/gpu/graph_eval.comp` | The kernel of section 8. |

### 10.1 Loader

`neura_gpu` compiles with `VK_NO_PROTOTYPES` and does not link the Vulkan library. `Context::create` opens it:

- Linux: `dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL)`.
- Windows: `LoadLibraryExA("vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)`.

It resolves `vkGetInstanceProcAddr` from the library and every other function through `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr`. A missing library or symbol makes the GPU unavailable with reason `Vulkan loader not found`. The library is not opened at all under `--no-gpu` or `NEURA_GPU=off`.

### 10.2 `Context`

```cpp
struct ContextOptions { bool allow_software = false; };

class Context {
public:
    // Returns nullptr and sets `reason` when no device is usable.
    static std::unique_ptr<Context> create(const ContextOptions& options, std::string& reason);
    const std::string& device_name() const;
};
```

Creation, in order:

1. Create an instance with `apiVersion = VK_API_VERSION_1_0`, application name `neura`, no layers, no extensions. Failure: `cannot create a Vulkan instance`.
2. Enumerate physical devices. A **candidate** has a queue family with `VK_QUEUE_COMPUTE_BIT` (the lowest-numbered one is used), `maxComputeWorkGroupInvocations ≥ 64`, `maxComputeWorkGroupSize[0] ≥ 64`, `maxPerStageDescriptorStorageBuffers ≥ 3`, `maxStorageBufferRange ≥ 2²⁰`, and a device type allowed by D10. The implementation checks these limits on the device; it must not assume the specification's minimums.
3. Order candidates by type rank (discrete 0, integrated 1, virtual 2, other 3, CPU 4 and only with `allow_software`), then by enumeration index. No candidate: `no usable Vulkan device`.
4. For each candidate in order, create the logical device with one queue and with `robustBufferAccess` enabled when the device reports it; create the shader module, a descriptor set layout with three `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` bindings for the compute stage, the pipeline layout, the compute pipeline, a command pool with `VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT`, and one fence; then run the self-test of section 10.5. The first candidate that passes is the device. A candidate that fails any step is destroyed and the next is tried. If all fail, the reason is `<device name>: <failure>` for the first candidate.

### 10.3 `VulkanGraphDevice`

`VulkanGraphDevice(Context&, const nvr::Image&)` implements `GraphDevice`. Construction runs the eligibility check, packs `TABLES` under the device's limits, uploads it once through a host-visible staging buffer and `vkCmdCopyBuffer`, and allocates `IO` and `WIRES` at their initial capacities.

| Buffer | Memory | Usage |
| --- | --- | --- |
| `TABLES` | `DEVICE_LOCAL` | Storage, transfer destination |
| `IO` | `HOST_VISIBLE` and `HOST_COHERENT`, preferring a type that is also `HOST_CACHED`; mapped for the buffer's life | Storage |
| `WIRES` | `DEVICE_LOCAL` | Storage |

If no memory type satisfies a row, or an allocation fails, construction leaves the device in the failed state of section 9.5 with the reason `cannot allocate device memory`. A context serves one `VulkanGraphDevice` at a time; the CLI creates one of each.

### 10.4 Dispatch protocol

For each dispatch:

1. Write the request records, input regions, and zeroed status words into the mapped `IO` buffer.
2. Submit a command buffer that binds the pipeline and the descriptor set and calls `vkCmdDispatch(R, 1, 1)`, with the fence. For `R = 1` the device reuses one command buffer recorded once; it is recorded again whenever a buffer is recreated, after the descriptor set is updated. For `R > 1` the command buffer is recorded for that dispatch.
3. Wait: poll `vkGetFenceStatus` for up to 2 ms, then call `vkWaitForFences` in 100 ms slices until the fence signals or 30 s have passed in total. Reset the fence.
4. Read each status word. For status 2, copy the output words to the job's `outputs`, sign-extending each from 32 bits, and set `evaluated`. For status 3, decline the job, count it in `escaped`, and demote its graph. Any other value fails the device.

Jobs for ineligible graphs are declined before a dispatch is built and never reach the device. Buffers grow only between dispatches. The steady clock used in step 3 serves only the wait; no run result depends on it.

After a timeout the device is not idle, so its Vulkan objects must not be destroyed. A device that failed by timeout skips all teardown and leaves its resources to process exit. Any other device waits for idle and destroys its objects in reverse creation order.

### 10.5 Self-test

`layered.hpp` exposes `self_test_graphs()`, a fixed list of small graphs with input sets, built in code. `Context::create` packs them, dispatches every request in one call, and compares every output and status with `evaluate_graph`. Expected values are computed at run time, never stored. The list must contain:

1. `relu(a + b - 1)` on all four inputs.
2. An identity neuron and a ReLU neuron with negative weights and a nonzero bias, on inputs that make the ReLU clamp and not clamp.
3. A chain of 200 neurons, each `relu(1 - previous)`, so that 200 layers and barriers execute.
4. One layer of 200 neurons, each reading a different input lane, followed by one identity neuron that sums all 200, so that a layer wider than a work group and a fan-in above 64 execute.
5. A graph whose output list repeats a wire and names an input wire.
6. An identity neuron computing `40000 × a`, which must return status 3 for `a = 1` and status 2 for `a = 0`.

The dispatch must hold at least 128 requests across these graphs. Any difference rejects the candidate with `self-test failed`. The same list runs against `ModelDevice` in the test suite.

## 11. Libraries and build

| Library | Change |
| --- | --- |
| `neura_nvm` | Adds `graph_device.cpp` and `layered.cpp`. Still links only `neura_nvr` and its existing platform libraries. No Vulkan dependency. |
| `neura_gpu` | New. Built when the GPU is enabled. Links `neura_nvm` and `${CMAKE_DL_LIBS}`. |
| `neura` | Links `neura_gpu` and is compiled with `NEURA_HAS_GPU=1` when the GPU is enabled. |

`CMakeLists.txt`:

- `set(NEURA_GPU "AUTO" CACHE STRING "GPU graph execution: AUTO, ON, or OFF")`. This CMake cache variable and the run-time environment variable of section 12.2 share a name and are unrelated.
- Locate the header with `find_path(NEURA_VULKAN_INCLUDE_DIR vulkan/vulkan.h)` and the compiler with `find_program(NEURA_GLSLANG glslangValidator)`.
- `AUTO` enables the GPU when both are found and the platform is not Apple, and otherwise disables it with a `message(STATUS ...)` naming what was missing. `ON` fails configuration with `message(FATAL_ERROR ...)` when either is missing. `OFF` disables it.
- The shader is compiled at build time with `glslangValidator -V --target-env vulkan1.0 -x -o <build dir>/generated/graph_eval.spv.inc src/gpu/graph_eval.comp` through `add_custom_command`, and `device.cpp` includes the result, a comma-separated list of 32-bit words, as the initializer of a `std::uint32_t` array. No generated file is checked in.

A runtime-only distribution that wants the GPU ships `neura_nvm` and `neura_gpu`; one that does not ships `neura_nvm` alone.

## 12. Command line

### 12.1 Options

These apply to `run` and `test` and are rejected on other commands with `<option> applies only to run and test`.

| Option | Meaning |
| --- | --- |
| (none) | Use a GPU when one is usable; otherwise the reference evaluator, silently. |
| `--no-gpu` | Use the reference evaluator. The Vulkan loader is not opened. |
| `--require-gpu` | Fail before entry unless a GPU device is created. |
| `--gpu-software` | Also accept software Vulkan implementations, ranked last. |
| `--gpu-verify` | Check every device evaluation against the reference evaluator and stop at the first difference. Implies `--require-gpu`. |

`--no-gpu` combined with `--require-gpu`, `--gpu-software`, or `--gpu-verify` is a usage error: `--no-gpu cannot be combined with <option>`.

### 12.2 Environment

`NEURA_GPU=off` has the effect of `--no-gpu` when none of `--require-gpu`, `--gpu-software`, and `--gpu-verify` is given; with any of them the variable is ignored. An unset or empty variable has no effect. Any other value is the error `NEURA_GPU must be "off" or unset`. The variable is read only by `run` and `test`.

### 12.3 Order of work

`run`: load and validate the image; create the context and the device; bind imports or read the replay trace; execute. `test`: build and load the image; create the context and the device once; run every test with it. The device pointer is placed in `options.limits.graph_device`, wrapped in a `VerifyingDevice` under `--gpu-verify`, and so reaches `nvm::run`, `nvm::record`, and `nvm::replay` alike. An invalid image still fails before any device or host service is touched.

### 12.4 Errors

All are printed as `neura: error: <text>` and exit with status 1.

| Case | Text |
| --- | --- |
| `--require-gpu` in a build without the GPU | `--require-gpu: this build has no GPU support` |
| `--require-gpu` and no device | `--require-gpu: <reason>` with the reason from section 10 |
| `--require-gpu` and the device failed during the run | `--require-gpu: the device failed during the run: <reason>`, printed after the run's own output; the exit status is 1 whatever the program returned |
| `--gpu-verify` found a difference | The `GraphDeviceMismatch` message |

Without `--require-gpu`, an unavailable or failed device is not an error and prints nothing.

### 12.5 `--stats`

`--stats` prints one more line to standard error after its existing output (after the `stats:` line of `run`, and once after the summary of `test`). The existing `stats:` line and the per-test figures on standard output do not change. The line is exactly one of:

```text
gpu: off
gpu: not built
gpu: unavailable (<reason>)
gpu: device "<name>", eligible graphs <E> of <G>, evaluated <n>, ineligible <n>, escaped <n>, failed <n>, dispatches <n>
```

`gpu: off` covers `--no-gpu` and `NEURA_GPU=off`. The counters are the fields of `GraphDeviceStatistics` over the whole process.

### 12.6 Usage text

`usage()` in `src/cli/main.cpp` adds `[--no-gpu | --require-gpu] [--gpu-software] [--gpu-verify]` to the `run` and `test` lines and these lines after the capability list:

```text
graph execution: run and test evaluate neuron graphs on a GPU when one is usable.
  --no-gpu                 evaluate graphs on the CPU
  --require-gpu            fail unless a GPU device is available
  --gpu-software           also accept software Vulkan implementations
  --gpu-verify             check every GPU evaluation against the CPU (implies --require-gpu)
```

## 13. Existing tests

`CMakeLists.txt` sets `ENVIRONMENT "NEURA_GPU=off"` on every existing CTest entry, by iterating the directory's `TESTS` property after the last existing `add_test`, before the new GPU entries are added. No existing test script, fixture, expected output, or baseline should need to change: `--stats` is used only by `tests/stdlib/area.cmake`, which compares standard output, and `tests/benchmarks/run.cmake`, which extracts figures by name, so the added `gpu: off` line on standard error affects neither. `tests/benchmarks/baseline.txt` is unaffected because steps, heap cells, neurons, and image bytes do not depend on the evaluator. If an existing test does fail in phase 3, the implementer reports it and does not edit the expectation.

## 14. Verification

Each entry names what it compares, over what domain, and what kind of evidence it is, in the terms of the [release checklist](../Release.md).

| CTest | What it establishes | Kind |
| --- | --- | --- |
| `backend-layered` (new suite in `neura_backend_tests`, always built) | Eligibility: a graph at the weight budget is accepted and one unit over is rejected; `INT64_MIN` weights and biases are rejected without overflow in the check; a graph without nodes is ineligible. Packing: every directory, node, edge, layer, and output word for hand-built graphs; layers respect every edge; `table_limit` skips a graph and keeps later ones. | Structural |
| `backend-layered` | `ModelDevice` against `evaluate_graph` for the integer circuits of `backend-circuits`: every pair of 8-bit operands for each operation, and the boundary and pseudo-random operands that suite uses for wider types. | Exhaustive (8-bit), sampled (wider) |
| `backend-layered` | Escape: the self-test graph 6 and a graph whose value is exactly `wire_bound` (accepted) and `wire_bound + 1` (escaped); demotion after the first escape; a graph whose reference evaluation overflows is never returned as evaluated. | Structural |
| `backend-layered` | `self_test_graphs()` against `ModelDevice`. | Structural |
| `backend-layered` | The trap cases of `backend-vm` built from `logic_model()` (non-binary output lane, invalid selector, graph overflow), run with `ModelDevice` wrapped in `VerifyingDevice`: the trap kind and detail text equal the run without a device. The overflow graph must be ineligible by `weight_budget`; the non-binary graph must be evaluated by the device. | Structural |
| `backend-layered` | Every image of `image_corpus(environment)` in `tests/backend/fixtures.hpp`, run through `run_captured` without a device and with `ModelDevice` wrapped in `VerifyingDevice`: `completed`, `status`, the trap's kind, reason, detail, function, block, and callers, `steps`, `host_requests`, `peak_heap_cells`, and the captured output are equal. | End to end |
| `gpu-device` (new, built with the GPU) | `VulkanGraphDevice` against `evaluate_graph` with the same circuit domains as `backend-layered`, submitted in dispatches of up to 65,535 requests; a mixed dispatch over several graphs; a call large enough to split into several dispatches; `IO` and `WIRES` growth. Uses `allow_software = true`. | Exhaustive (8-bit), sampled (wider) |
| `gpu-cli` (new CMake script) | For each program under `tests/backend/programs` and `tests/backend/traps` whose `--no-gpu --stats` run takes at most 200,000 steps: stdout, exit status, and stderr without the `gpu:` line are identical between `--no-gpu` and `--require-gpu --gpu-software --gpu-verify`. A trace recorded with `--no-gpu` replays with `--require-gpu --gpu-software`, and the reverse. The option errors of section 12. `NEURA_GPU=off` and its precedence. Programs over the step bound are listed in the log. | End to end |

`gpu-device` and `gpu-cli` exit with status 77 and are registered with `SKIP_RETURN_CODE 77` when no Vulkan device, hardware or software, can be created, so CTest reports them as skipped and never as passed. Both carry the CTest label `gpu` and a timeout of 1,800 seconds.

**Limits of this evidence.** These are observed agreements over the stated domains. The argument of section 6.4 is a proof about the algorithm under the stated arithmetic; it is not a proof about any driver. A machine with only llvmpipe verifies the Vulkan path against a software implementation; it does not verify any hardware driver. The self-test runs on every start and covers six graphs. A handoff must state which devices `gpu-device` and `gpu-cli` actually ran on, by the `gpu:` line, and name any that were skipped.

## 15. Documentation to update with the implementation

| Page | Change |
| --- | --- |
| [NVM](../runtime/NVM.md) | In "Graphs and continuations", after the profile paragraph: an implementation may compute a graph's output wires by another method when the values equal the reference rule's and the reference rule does not overflow. A new section "Graph devices" with the contract of section 5 and a link here. |
| [Backend](../Backend.md) | The directory table gains `src/gpu/`; "Running an image" gains the options, the fallback rule, and the embedding field; the test table gains the new entries. |
| [Tooling](Tooling.md) | The options of section 12, the environment variable, the `gpu:` line, and the build requirement for `NEURA_GPU`. |
| [Status](Status.md) | The Tooling row lists the new options. |
| [Release](../Release.md) | A row under "Toolchain and formats" mapping GPU graph execution to `backend-layered`, `gpu-device`, and `gpu-cli` with their kinds. |
| [Project](../Project.md) | One sentence in "Runtime boundary": graph evaluation may run on a GPU with results identical to the reference evaluator. |
| `wiki/` | The matching pages, following [wiki maintenance](../Wiki.md). |
| This page | Becomes the specification: remove "plan", move section 4 to the measurements a release actually records, and keep sections 5 to 12 as the contract. |

## 16. Cost today, and what would change it

D1 makes the GPU the default for one sequential execution that evaluates one graph per step. For that workload the measurements of section 4 say:

- Every `neura run` and `neura test` process pays roughly 40 to 130 ms to start the device before the first step.
- Every step that reaches the device costs 22 to 6,500 microseconds, where the reference evaluator costs 0.08 to 57. On the measured discrete GPU that is 22 to 309 times slower per graph.
- As an estimate, not a measurement: the sort benchmark at 20,000 elements takes 4.84 million steps and 1.03 s on the CPU. At the smallest measured round trip of 23.5 microseconds per step it would take at least 110 s on the GPU, and more with its real mix of graphs.

Results are identical either way, and `--no-gpu` or `NEURA_GPU=off` restores the CPU cost. The implementer must not add a threshold, cost model, or cache to soften these numbers (D7); doing so changes which evaluator runs and belongs to the owner.

The same measurements show where the GPU wins: many requests per dispatch on a discrete GPU, at 2.2 to 27 times the CPU's rate per request. Nothing in the current runtime produces such batches. Three changes would, each needing its own design and none part of this plan:

1. **Many executions at once.** A runner that advances many independent executions in lockstep and submits their current graphs in one dispatch: exhaustive and differential verification, the decompiler's exhaustive lifting, and `neura test` if host effects are committed in test order. The `GraphDevice` interface already takes a span of jobs for this.
2. **Shallower circuits.** Kernel time grows by one to two microseconds per layer, and the graphs measured in section 4 have up to 4,290 layers with fewer than ten neurons in an average layer. Parallel-prefix carries and tree reduction would trade more neurons for far fewer layers. That is an IRtoNeuronConversion change with its own cost contract; [Backend](../Backend.md#circuits) documents the ripple-carry adder and restoring division used today, and [Project](../Project.md#neurair-and-optimization) lists graph depth among the circuit costs.
3. **Parallelism in the language.** [Concurrency](ResourcesConcurrency.md) is deferred. A data-parallel construct would give one program many frames evaluating the same graph.

## 17. Phases

Each phase is one reviewable change and leaves every existing test passing.

| Phase | Work | Done when |
| --- | --- | --- |
| 1 | `graph_device.hpp`, `layered.*`, `ModelDevice`, `VerifyingDevice`, the `RunLimits` field, and the `evaluate` change of section 9.2. No CLI change. | `backend-layered` and the `backend-vm` and `backend-reference` additions pass. All existing tests pass unchanged. `neura` behaves exactly as before. |
| 2 | `NEURA_GPU` in CMake, `neura_gpu`, the kernel, `Context`, `VulkanGraphDevice`, and the self-test. CLI: the four options exist; the default is still the reference evaluator, and a device is created only with `--require-gpu` or `--gpu-verify`. | `gpu-device` passes on at least one named device. The option errors behave as section 12.4 states. A build with `NEURA_GPU=OFF` passes all tests. |
| 3 | The default of D1, `NEURA_GPU=off`, the environment setting on existing tests (section 13), the `gpu:` line, `gpu-cli`, and the documentation of section 15. | `gpu-cli` passes on at least one named device. The full suite passes with the GPU built and with `NEURA_GPU=OFF`. The handoff lists the devices used and any skipped entry. |

## 18. Outside this plan

- Moving frames, exits, state operations, or the heap to the GPU.
- Any change to circuits, the image format, or the graph profile.
- Batched runners, parallel test execution, and GPU use by the decompiler or the build.
- Device selection by the user, multiple devices, and pipeline caches.
- macOS, and any API other than Vulkan.

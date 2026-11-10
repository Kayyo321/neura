# GPU execution of neuron graphs

This page specifies how `neura run` and `neura test` can evaluate neuron graphs on a GPU, how many executions are advanced together so that a GPU has enough work to be worth using, and how an automatic mode decides, by speed alone, whether to use a GPU at all and for which graphs. The NVM exposes graph devices and fleets to embedders. The CLI defaults to automatic placement and can also select explicit Vulkan or CPU-only execution. The [NVM specification](../runtime/NVM.md) and [NVR format](../runtime/NVR.md) remain the execution contracts.

## 1. How to read this specification

- "Must" states a requirement on the implementation. A constant, name, message, or layout given here is fixed. An implementer who finds one unworkable stops and reports it; they do not pick a replacement.
- Section 3 lists every decision. Section 4 gives the measurements those decisions rest on, and says which are measurements and which are estimates.
- Source references describe the tree at commit `b48d163`, which has host callbacks (`Execution::invoke_callback`).

Terms:

| Term | Meaning |
| --- | --- |
| Reference evaluator | `nvm::evaluate_graph` in `src/nvm/machine.cpp`. It defines the result of evaluating a graph. Every other evaluator is correct only where it returns exactly what the reference evaluator returns. |
| Request, job | One evaluation of one graph on one set of input lanes. |
| Dispatch | One submission to the GPU carrying one or more requests. |
| Member | One execution inside a fleet. |
| Fleet | A set of independent executions of one image that are advanced together (section 10). |
| Round | One step of every member of a fleet that is able to step. |
| Placement | The choice, for each job of a round, between the device and the reference evaluator. |
| Mode | `auto`, `always`, or `off` (section 14). |

**Stages affected.** Only the runtime after the output stage: the NVM library, a new GPU library, and the CLI. The lexer, parser, NeuraIR, IR optimization, IRtoNeuronConversion, the NVR writer and loader, and the decompiler do not change. The image format, the graph profile, and every image byte stay the same.

## 2. Summary

Each NVM step resolves a block's inputs, evaluates the block's graph, decodes the selected exit, and commits it ([block transition](../runtime/NVM.md#execution-state)). This plan lets the second action, graph evaluation, run on a Vulkan compute device. Everything else stays in the CPU NVM: input resolution, the selector and lane checks, exits, frames, the heap, garbage collection, limits, host calls, traces, and every trap diagnostic.

The GPU evaluates a graph layer by layer in exact 32-bit integer arithmetic. A static check on each graph's weights and a run-time bound on every neuron's value together guarantee that the 32-bit result equals the reference evaluator's checked 64-bit result; a request that leaves the bound is recomputed by the reference evaluator. No floating-point operation is involved in a result, so results do not depend on the device.

Dispatch has a fixed cost. A GPU can repay it by evaluating many requests together. Two mechanisms serve that purpose:

- **Batched running** (section 10). A fleet advances many executions of one image in rounds and offers each round's graphs to the device as one batch. Host effects and results surface in member order, so a fleet cannot be told apart from running its members one after another. `neura test` runs its tests as a fleet, and `neura run --each` runs one image over many argument lists as a fleet.
- **Auto mode** (section 11), the default. The runtime predicts the cost of each choice from a measured model of the device and of the CPU. It leaves the Vulkan loader unopened until the workload could repay opening it, discards a device that cannot beat the CPU, and afterwards sends a round's graphs to the device only when that is predicted to be clearly faster.

Whatever is chosen, a run must be indistinguishable to the program and to anyone reading its output, status, trap diagnostics, step counts, heap figures, and host traces.

## 3. Decisions

| ID | Decision |
| --- | --- |
| D1 | There are three modes. `auto`, the default, lets the runtime decide by predicted speed. `always` sends every eligible graph to a GPU. `off` uses the reference evaluator only. `--no-gpu` selects `off`. |
| D2 | A device performs graph evaluation only. Frames, slots, exits, state operations, the heap, collection, limits, host requests, and trap construction stay in `Execution::State`. |
| D3 | No NVR, NIR, or graph profile change. Profile 1 keeps its definition: checked signed 64-bit arithmetic, ascending-source edge order, stored node order. |
| D4 | The device API is Vulkan 1.0 compute with no extensions and no layers. The Vulkan loader is opened at run time; the build needs only Vulkan headers and `glslangValidator`. |
| D5 | GPU arithmetic is 32-bit two's-complement integer. Exactness comes from the weight budget and the wire bound of section 6, with `wire_bound = 32767`. |
| D6 | The kernel is the layered kernel of section 8: one work group per request, 64 invocations per work group, one barrier per layer. Its source in section 8 is normative. |
| D7 | Placement is fixed by the mode. In `always`, every job for an eligible graph goes to the device. In `auto`, placement is exactly the procedure of section 11. In `off`, nothing does. A graph with no neurons is never sent. The implementer adds no other threshold, sampling rule, or cache. |
| D8 | When no usable device exists, `auto` and `always` use the reference evaluator and print nothing. `--require-gpu` turns that case into an error before entry. |
| D9 | The NVM library's default stays the reference evaluator. `nvm::Execution`, `nvm::run`, `nvm::record`, `nvm::replay`, and `nvm::Fleet` use a device only when the caller passes one. Only the CLI applies D1. |
| D10 | Device choice is deterministic: discrete, then integrated, then virtual, then other, each in loader enumeration order. Software implementations (`VK_PHYSICAL_DEVICE_TYPE_CPU`) are used only with `--gpu-software`. |
| D11 | The existing CTest entries run with `NEURA_GPU=off`, so their evidence continues to come from the reference evaluator, one execution at a time, and their duration does not depend on the machine. New entries cover fleets, placement, and the GPU path. |
| D12 | macOS is outside this plan. The CMake option `NEURA_GPU=AUTO` resolves to `OFF` on Apple platforms. The Windows path is specified and unverified, as MSVC builds are today ([Backend](../Backend.md#tests-and-the-limits-of-the-evidence)). |
| D13 | A fleet delivers host requests and results in member order: a member's host request is issued only after every lower-numbered member has finished. A fleet is therefore equivalent to running its members one after another. |
| D14 | `neura test` and `neura run --each` run their members as a fleet in `auto` and `always`, and one at a time in `off`. A `neura run` without `--each` executes one program and never forms a fleet. |
| D15 | In `auto`, the decision to open a device is a deterministic function of the image and of the jobs the run produces. Decisions made after that use measured time, so they can differ between runs and machines. No decision can change a result. |
| D16 | `auto` prefers the CPU when in doubt: the device must be predicted to take at most 0.75 of the CPU's time. A rejection lasts for the rest of the process. Nothing is cached on disk and no thread is created. |
| D17 | `neura run --each <file>` is the command-line way to give one image many inputs (section 10.11). It is defined in every mode and build, including builds without the GPU. |

## 4. Release measurements

### 4.1 Machine

Linux x86-64, AMD Ryzen 7 9800X3D, GCC 14.2.0 Release build, NVIDIA GeForce RTX 5080 with driver 620.70.06 and Vulkan 1.4.357. The single-run and standard-library timings pinned the CLI to CPU 7. Other Vulkan devices on the same host were AMD Radeon Graphics (RADV RAPHAEL_MENDOCINO) and llvmpipe (LLVM 19.1.7, 256 bits).

### 4.2 Workloads

The single-execution runs used every program in `tests/benchmarks`, at both sizes declared in each source file. The test runs used every top-level test source in the `array-deque`, `core`, and `integers` standard-library areas. The 65,536-member run used `tests/backend/each/collatz.nra` on decimal arguments 1 through 65,536. It ran 37,613,886 steps, made 65,536 host requests, and reported a largest member heap peak of 223 cells. The image has 50 graphs, 27,446 neurons, and 115,162 edges.

### 4.3 Single-execution timing

Each row is the median of nine paired wall-time samples after warming each mode. The image was built once. Every auto run reported `gpu: auto, not used (no graph can gain at this batch size)`. Output and non-GPU statistics matched byte for byte.

| Workload | CPU only (s) | Auto (s) | Auto / CPU |
| --- | ---: | ---: | ---: |
| array-list/1000 | 0.008250 | 0.008392 | 1.017127 |
| array-list/2000 | 0.014638 | 0.014819 | 1.012362 |
| format/100 | 0.024562 | 0.024696 | 1.005452 |
| format/200 | 0.047325 | 0.047312 | 0.999716 |
| hash-map/1000 | 0.031394 | 0.031346 | 0.998471 |
| hash-map/2000 | 0.069701 | 0.069786 | 1.001219 |
| json/100 | 0.019332 | 0.019577 | 1.012637 |
| json/200 | 0.037311 | 0.037262 | 0.998681 |
| sha256/10240 | 0.180542 | 0.181026 | 1.002677 |
| sha256/20480 | 0.356972 | 0.357439 | 1.001308 |
| sort/1000 | 0.038903 | 0.038751 | 0.996100 |
| sort/2000 | 0.082329 | 0.082323 | 0.999927 |
| strings/10000 | 0.044804 | 0.045229 | 1.009478 |
| strings/20000 | 0.087473 | 0.086792 | 0.992217 |
| time-text/100 | 0.120894 | 0.121812 | 1.007594 |
| time-text/200 | 0.234959 | 0.235724 | 1.003257 |
| tree-map/1000 | 0.046747 | 0.047356 | 1.013027 |
| tree-map/2000 | 0.096395 | 0.097303 | 1.009418 |

### 4.4 Reproducing the measurements

Use the [release measurement harness](../../tests/benchmarks/gpu-release.py) for the single-execution and test timings, and the [Collatz harness](../../tests/benchmarks/gpu-collatz.py) for the argument-list workload. Run on an idle machine with access to the discrete Vulkan device.

```sh
env -u DISPLAY -u WAYLAND_DISPLAY taskset -c 7 python3 tests/benchmarks/gpu-release.py --neura build-gpu/neura --samples 9
env -u DISPLAY -u WAYLAND_DISPLAY python3 tests/benchmarks/gpu-collatz.py build-gpu/neura --cpu 7
```

### 4.5 Fleet timing

The test area times include compilation and test execution. Each row is the median of nine paired samples.

| Workload | CPU only (s) | Auto (s) | Auto / CPU |
| --- | ---: | ---: | ---: |
| array-deque | 2.154411 | 2.159302 | 1.002270 |
| core | 0.019096 | 0.019206 | 1.005720 |
| integers | 1.476595 | 1.566640 | 1.060982 |

On 65,536 Collatz argument lists, CPU-only wall time was 17.072696 s and auto wall time was 18.878298 s, a ratio of 1.105760. Both exited with status 0 and matched the independent integer oracle and each other's non-GPU statistics. The pinned pair missed the 1.0 release bound.

```text
gpu: auto, device "NVIDIA GeForce RTX 5080", eligible graphs 48 of 50, evaluated 930920, kept on cpu 32183194, ineligible 0, escaped 0, failed 0, dispatches 421, correction 1.13
```

The pair with all eight cores available took 17.719397 s on CPU and 19.431556 s in auto, a ratio of 1.096626.

```text
gpu: auto, device "NVIDIA GeForce RTX 5080", eligible graphs 48 of 50, evaluated 942829, kept on cpu 32171285, ineligible 0, escaped 0, failed 0, dispatches 435, correction 1.19
```

### 4.6 Calibration and fixed model fixtures

A release-device calibration run on the RTX 5080 reported these coefficients. The calibration test checked every probe output against reference evaluation.

```text
a=10.27 b0=0.3149109313 b1=0.1514894593 K=1893.394617 d=0.0006639245512 c0=0.007616667686 c1=0.0003803637066 c2=0.0008310936075
```

The structural model-fitting test uses the fixed design probe times 9.2, 253.9, 4605.6, 15.2, 371.9, and 152.6 µs. These are test inputs from the design measurements. The expected device coefficients are `a = 9.2`, `b0 = 0.343`, `b1 = 0.135`, `K = 2,123`, and `d = 0.000259`. The verdict tests also use the design llvmpipe coefficients `a = 25.1`, `b0 = 0.416`, `b1 = 0.007`, `K = 5`, and `d = 0.001895`.

### 4.7 Correctness on devices

The integer circuit suite passed on the RTX 5080 and the integrated Radeon. On both, the mixed 65,536-request call split into two dispatches. The CLI suite ran on the RTX 5080, including byte-identical trace recording and replay across evaluators. The llvmpipe context passed the startup self-test.

## 5. Contract

### 5.1 What must be identical

For the same image, arguments, limits, and host completions, an execution must agree in every mode, with any device, and whether it runs alone or as a member of a fleet, on:

- every exit selection, slot assignment, state operation, and host request, in order, with the same arguments;
- the terminal outcome: status, or the trap's kind, reason, detail text, function, block, and caller list;
- `steps`, `peak_heap_cells`, `function_steps`, and the host request count;
- standard output and the trap diagnostic on standard error;
- a trace written by `--record`, byte for byte, and the result of `--replay`.

For one execution this follows from the device contract in section 9.1, because a device replaces only the computation of the output lane values that `read` and `select` consume. For a fleet it follows in addition from section 10.6.

### 5.2 What may differ

- Wall time and process start-up time.
- Which evaluator computed a given graph, and therefore the `gpu:` line that `--stats` adds (section 14.5).
- Runner errors that exist only for the GPU options (section 14.4).
- Peak memory of the process: a fleet holds several members' heaps at once (section 10.7).

### 5.3 What stays in the circuit and what crosses the boundary

The circuit is unchanged: the same graphs, weights, biases, and activations decide every arithmetic result and every exit. A device is not a host service. It has no import, no capability, no entry in a host trace, and the program cannot observe it. It is an internal implementation choice of the kind [NVM](../runtime/NVM.md#execution-state) permits when observable behavior is identical. Data that crosses to the device per request: the graph's index, its input lanes as zeros and ones, and back, the values of its output lanes and one status word. Reference handles cross only as the 64 bit lanes a graph already receives; the device never dereferences them.

Auto mode reads a clock and does floating-point arithmetic. Both are confined to placement: they choose which evaluator runs, and both evaluators return the same values.

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
| `IO` capacity | Starts at the larger of 1 MiB and four bytes times `5 + max(input_lanes + output_count)` over eligible graphs. Grows to the next power of two that fits a dispatch, up to 256 MiB and subject to the device's `maxStorageBufferRange`. |
| `WIRES` capacity | Starts at the larger of 1 MiB and four bytes times the largest eligible `wire_count`. Grows the same way, up to 256 MiB and subject to the device's `maxStorageBufferRange`. |
| Requests per dispatch | At most the smaller of 65,535 and the device's `maxComputeWorkGroupCount[0]`. |

A call with more requests than one dispatch can hold is split into consecutive dispatches, each taking the longest prefix of the remaining requests that fits all three limits. The single-request path of `Execution` always fits the initial capacities, so it never reallocates.

### 7.5 Graph shapes

`layered.hpp` also exposes one `GraphShape` per graph of an image, eligible or not, for the cost model of section 11:

| Field | Symbol | Meaning |
| --- | --- | --- |
| `eligible`, `reason` | | The result of section 6.2 |
| `layers` | `L` | `layer_count` |
| `edges` | `E` | Total edges |
| `nodes` | `N` | Total nodes |
| `critical` | `C` | Critical work, below |
| `io_words` | `W` | `input_lanes + output_count + 5`: the `IO` words one request occupies |

**Critical work.** In a layer, invocation `i` of the kernel handles the nodes at positions `first + i`, `first + i + 64`, and so on, and its work is the sum of their edge counts. A layer's critical work is the largest such sum over `i` from 0 to 63. `C` is the sum of the layers' critical work. It measures the longest chain of edge evaluations a request needs when the device is otherwise idle.

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

The device interface in `src/nvm/graph_device.hpp`, library `neura_nvm`:

```cpp
namespace neura::nvm {

enum class IneligibleReason { none, no_nodes, weight_budget, table_limit, escaped };

struct GraphShape {
    bool eligible = false;
    IneligibleReason reason = IneligibleReason::none;
    std::uint64_t layers = 0;
    std::uint64_t edges = 0;
    std::uint64_t nodes = 0;
    std::uint64_t critical = 0;
    std::uint64_t io_words = 0;
};

struct GraphJob {
    std::uint32_t graph = 0;
    std::span<const std::int64_t> inputs;
    std::span<std::int64_t> outputs;
    bool evaluated = false;
};

namespace detail {
using MaterializeGraphJobs = std::function<std::span<GraphJob>(std::span<const std::size_t>)>;
} // namespace detail

struct GraphDeviceStatistics {
    std::uint64_t evaluated = 0;
    std::uint64_t declined_ineligible = 0;
    std::uint64_t declined_placement = 0;
    std::uint64_t escaped = 0;
    std::uint64_t declined_failed = 0;
    std::uint64_t dispatches = 0;
};

// A device belongs to one image object. The image and its graph table must
// stay alive and unchanged while the device is in use.
class GraphDevice {
public:
    virtual ~GraphDevice() = default;
    virtual const nvr::Image& image() const = 0;
    // For evaluated jobs, return the reference output lanes exactly and only
    // when the reference computation does not overflow. On device failure or
    // when declining a job, clear evaluated and leave its outputs unspecified.
    // An invalid graph index, lane count, or input bit is a caller error.
    virtual void evaluate(std::span<GraphJob> jobs) = 0;
    // The largest useful batch. A hint of one gives no benefit from batching.
    virtual std::size_t batch_hint() const = 0;
    virtual GraphDeviceStatistics statistics() const = 0;
    // Optional observations for a placement wrapper. An empty shape span
    // leaves eligibility to the wrapper's static analysis of the image.
    virtual std::span<const GraphShape> graph_shapes() const { return {}; }
    virtual std::string_view failure_reason() const { return {}; }

private:
    friend class Fleet;
    // Fleet has resolved binary inputs for every graph in this order. An
    // opting-in device may synchronously materialize a single nonempty
    // subset of strictly increasing job indices. Returning false without
    // materializing leaves all work to the caller's ordinary eager batch.
    virtual bool evaluate_prepared(std::span<const std::uint32_t>, const detail::MaterializeGraphJobs&) { return false; }
};
} // namespace neura::nvm
```

`graph_shapes()` returns one shape per graph in image order when supplied. It includes current table admission and escape demotion. An empty span means the wrapper uses the image's static shapes. A nonempty `failure_reason()` indicates a failed device and retires it from automatic placement. The view belongs to the device.

The private prepared hook is called only by a fleet. The materializer may be called once, synchronously, with a nonempty strictly increasing subset of valid job indices. Returning true declines any job that was never materialized. Returning false requires that no jobs were materialized, and the caller takes the eager path. The callback and any views it supplies are valid only during the call.

Contract of `evaluate`, for every job:

- If it sets `evaluated` to true, then `outputs[k]` equals the value the reference evaluator assigns to wire `graph.outputs[k]` for those inputs, for every `k`, and the reference evaluator reports no overflow for those inputs.
- Otherwise it sets `evaluated` to false and the contents of `outputs` are unspecified. The caller must then use the reference evaluator.
- A device failure never throws; the device declines the job.
- `graph` out of range, `inputs.size() != input_lanes`, `outputs.size() != graph.outputs.size()`, or an input other than 0 or 1 is a caller bug and throws `std::invalid_argument`.

A device belongs to one image object. It reads `image.graphs` once, when constructed, and must not be used after the image is destroyed or its graph table changes. The jobs of one call are independent; a device may evaluate them in any order or together.

### 9.2 Splitting a step

Today `Execution::State::step` evaluates the graph into `wires_` and then `select` and `read` index `wires_[graph.outputs[i]]`. The step is split at that point so that a caller can supply the output lane values.

`src/nvm/machine.hpp` gains a free function, the only place the reference evaluator is turned into output lanes:

```cpp
// Evaluates `graph` on `inputs` with the reference evaluator and stores wire
// graph.outputs[k] in outputs[k]. `scratch` is working storage. Returns the
// index of the node whose weighted sum overflows, or nothing.
std::optional<std::size_t> evaluate_outputs(const nvr::Graph& graph, std::span<const std::int64_t> inputs,
                                            std::vector<std::int64_t>& scratch, std::span<std::int64_t> outputs);
```

`Execution` gains:

```cpp
struct GraphStep {
    std::uint32_t graph = 0;                 // index into image.graphs
    std::span<const std::int64_t> inputs;    // the block's input lanes; valid until the step is finished
    std::size_t output_count = 0;            // graph.outputs.size()
};

std::optional<Event> begin_step(GraphStep& step);
std::optional<Event> finish_step(std::span<const std::int64_t> outputs);
Event fail_step(std::size_t node);
std::uint64_t live_heap_cells() const;
```

- `begin_step` returns the final event again when the run has finished. It throws `std::logic_error` while a host request is pending or a step is already begun. Otherwise it starts the run if needed, applies the step limit, counts the step and the profile entry, resolves the block's inputs through `read`, writes the lanes into a member vector `inputs_`, and fills `step`. A trap in any of these is returned as the `Trapped` event and ends the run, exactly as today.
- `finish_step` copies `outputs` into a member vector `outputs_`, then does everything `step` does today after `evaluate`: `select`, the assignment and argument reads, the parallel commit, and the exit action. It returns nothing when the run continues, and otherwise the `HostRequest`, `Completed`, or `Trapped` event.
- `fail_step` raises `GraphOverflow` with the detail `graph <g> node <n>` for the begun step and returns the `Trapped` event.
- `select` and `read` change in one way: where they read `wires_[graph.outputs[i]]` they read `outputs_[i]`. Their checks and messages are unchanged.
- `live_heap_cells` returns the current value of the counter that `peak_heap_cells` is the maximum of.

The private `step` becomes: `begin_step`; evaluate; `finish_step` or `fail_step`. Evaluation inside `step` is: when `limits_.graph_device` is set and the graph has at least one node, call the device with that single job and use its outputs if it comes back evaluated; otherwise `evaluate_outputs` with the execution's own scratch vector. `advance` and `invoke_callback` keep calling `step`, so callbacks and single executions behave as before.

`RunLimits` gains one field, after `profile`:

```cpp
// Evaluates graphs for this run, or nullptr for the reference evaluator.
// The device must have been created for the image the run executes.
GraphDevice* graph_device = nullptr;
```

Every `Execution::State` constructor throws `std::invalid_argument("graph device was created for another image")` when `limits.graph_device` is set and `&limits.graph_device->image() != &image`.

The order of faults within a step does not change: step limit, input reads, graph overflow, selector, assignments, arguments, commit, action. `nvm::run`, `nvm::record`, and `nvm::replay` already pass `RunLimits` through to `Execution` and need no change.

### 9.3 `ModelDevice`

`src/nvm/layered.hpp` also declares `ModelDevice`, a `GraphDevice` that runs the kernel's algorithm on the CPU from the same `TABLES` words: 32-bit arithmetic, layer order, the escape rule, the demotion rule, and the status values. It processes a layer's positions in ascending order, and its `batch_hint` is the largest `std::size_t`. It exists so that eligibility, packing, the exactness argument, fleets, and placement are tested everywhere, including machines without Vulkan. It is not used by the CLI.

Each call models one dispatch. An escape demotes the graph for later calls. Jobs already in the same dispatch still produce their individual status, as on the device.

### 9.4 `VerifyingDevice`

`src/nvm/graph_device.hpp` declares `VerifyingDevice`, which wraps another device. For every job the inner device evaluates, it runs `evaluate_outputs` on the same inputs and compares. On a difference it throws `GraphDeviceMismatch`, a `std::runtime_error`, with one of these messages:

```text
graph device mismatch: graph <g> output <k>: device <value>, reference <value>
graph device mismatch: graph <g>: reference evaluation overflows at node <n>
```

The exception leaves `Execution::advance()` or `Fleet::advance()`, which catch only traps, and ends the run. `image()`, `batch_hint()`, and `statistics()` forward to the inner device.

### 9.5 Device failure

A device **fails** when a Vulkan call returns an error, a dispatch does not complete within 30 seconds, or a status word is neither 2 nor 3. From then on it declines every job, including the rest of the failing call, counts them in `declined_failed`, and keeps a one-line reason. The run continues on the reference evaluator with identical results. A failed device is never retried.

## 10. Batched running

### 10.1 Purpose

Section 4.5 shows that a dispatch costs almost the same for one request as for several hundred. A single execution produces one request per step, so it can only ever fill a dispatch with one. A fleet advances many executions of one image a step at a time and hands all of that round's requests to the device in one call.

Where the members come from in this plan:

| Consumer | Members |
| --- | --- |
| `neura test` (section 10.10) | One per test of the listed files |
| `neura run --each` (section 10.11) | One per argument list in a file |
| Embedders and tests of the toolchain, through `nvm::Fleet` | Any set of entry points or scalar function calls of one image, for example every input of an exhaustive sweep |

A `neura run` without `--each` executes one program, so it has one execution and no fleet. Section 18 lists other consumers that this plan does not add.

### 10.2 Entry points

`neura test` currently selects a test by writing `image.entry` before each run. Members that are alive together cannot share that field, so `Execution` gains a third constructor:

```cpp
// Starts at `entry`, which must have the image entry signature (one array of
// text, returning int32), with the given arguments. Throws
// std::invalid_argument for another signature or for an argument that is not
// valid UTF-8.
Execution(const nvr::Image& image, std::uint32_t entry, std::span<const std::string> arguments,
          RunLimits limits = {});
```

It behaves exactly like the first constructor with `image.entry` equal to `entry`.

### 10.3 `Fleet`

New files `src/nvm/fleet.hpp` and `fleet.cpp`, library `neura_nvm`:

```cpp
namespace neura::nvm {

struct FleetOptions {
    RunLimits limits;                 // applied to every member
    GraphDevice* device = nullptr;    // receives each round's jobs; nullptr for the reference evaluator
    std::uint32_t window = 4096;      // most members considered per round
};

struct FleetEvent {
    std::size_t member = 0;
    Event event;
    RunResult result;                 // filled when event.kind is Completed or Trapped
};

class Fleet {
public:
    Fleet(const nvr::Image& image, FleetOptions options);

    // Adds a member that starts at an entry-signature function, or at a
    // function with scalar parameters. Returns its index. Members are
    // numbered from zero in the order added.
    std::size_t add(std::uint32_t entry, std::vector<std::string> arguments);
    std::size_t add_call(std::uint32_t function, std::vector<std::uint64_t> arguments);

    std::size_t size() const;
    bool done() const;                // every member's final event has been returned
    FleetEvent advance();
    void complete(const HostCompletion& completion);
    std::optional<HostValue> invoke_callback(std::string_view name, std::uint32_t version,
                                             const std::vector<HostValue>& arguments,
                                             const Execution::HostHandler& host);
    std::uint64_t rounds() const;
};

}
```

- The constructor throws `std::invalid_argument` when `options.window` is 0, when `options.limits.graph_device` is not null (`a fleet's device is given in FleetOptions`), or when `options.device` was created for another image.
- `add` and `add_call` validate their arguments as the corresponding `Execution` constructors do and throw the same exceptions. They throw `std::logic_error` after the first `advance`.
- `FleetEvent::result` carries `completed`, `status`, `trap`, `steps`, `peak_heap_cells`, and `function_steps`. `host_requests` is zero; `run_fleet` (section 10.9) fills it.
- `advance` throws `std::logic_error` when the fleet is done or while a request it returned has not been completed.

### 10.4 Member states

| State | Meaning |
| --- | --- |
| Waiting | Not started. No `Execution` exists. |
| Runnable | Started and able to step. |
| Parked | Its last step selected a host call. The `HostRequest` event is stored. |
| Awaiting | Its `HostRequest` has been returned to the caller and not yet completed. Only the head can be in this state. |
| Finished | Completed or trapped. The event and result are stored and the `Execution` is destroyed. |
| Reported | Its final event has been returned to the caller. |

The **head** is the lowest-numbered member that is not Reported.

### 10.5 `advance`

`advance` repeats these steps until one of them returns.

1. If the head is Finished, mark it Reported and return its final event with its result.
2. If the head is Parked, mark it Awaiting and return its stored `HostRequest`.
3. Compute the width `w`: `options.window`, and when a device is set, the smaller of that and `max(1, device->batch_hint())`.
4. Form the **span**: starting at the head and going up by index, the first `w` members that are not Finished. For each Waiting member of the span, in index order, start it (construct its `Execution`) if the memory rule of section 10.7 allows; the first Waiting member that may not start ends the span there.
5. Run one **round** over the Runnable members of the span, in index order:
   1. With no device, use the private reference step path. It charges the step, reads inputs into shared scratch, and evaluates and finishes the step. Settle any resulting event.
   2. With a device, use the private prepare path. It charges the step and checks every input read. If it returns an event, settle the member with it. If the graph has no nodes, evaluate it by reference and settle the step. Otherwise remember the member and graph index.
   3. If any jobs were remembered, offer their graph indices in member order to the private prepared hook. If the hook handles the batch, size the output arena and pack binary inputs only for its selected indices. If the hook declines the batch, size the arena and materialize every remembered job, then call `device->evaluate` once in member order. An ordinary device or the verifier takes this eager path.
   4. For each remembered member in index order, finish the step with its device output if evaluated. Otherwise pack its inputs directly into shared reference scratch, evaluate by reference, and finish the step or report the first overflow with `fail_step`. Settle any resulting event.
6. Add one to the round counter and go to step 1.

The private prepare path checks the same reads and step limit as `begin_step`. Frames cannot change between the checks and packing. The shared reference scratch and the output arena are temporary. Finishing the step consumes the same lanes or overflow in the same member order as the eager procedure.

With no device and no waiting members, the fleet can group rounds. It advances the head by reference to its next event, then advances each other Runnable member for that many rounds or until its first event, whichever comes first. Each grouped step uses the same reference evaluation and checks. Because members share no mutable execution state and there is no intervening admission or host response, the group preserves the resulting member states, observable event order, and round count.

**Settle** a member with the optional event of its step: nothing leaves it Runnable; a `HostRequest` makes it Parked and stores the event; `Completed` or `Trapped` makes it Finished, copies its result out of the `Execution`, and destroys the `Execution`.

`complete` requires the head to be Awaiting, forwards to the head's `Execution::complete`, and makes it Runnable. If that completion ended the run (a process exit, or an ill-typed response), the head's next step returns the final event and settles it. `invoke_callback` requires the head to be Awaiting and forwards to the head's `Execution::invoke_callback`.

### 10.6 Why a fleet equals running its members in order

1. Members share nothing that changes. The image is constant. Each `Execution` owns its frames, heap, globals, handles, identity numbers, and counters, and its limits apply to it alone.
2. A step's result depends only on the member's own state, because the output lanes it receives are the reference values whichever evaluator produced them (section 9.1).
3. Host services are invoked only by the caller, and the caller is only ever given the head's requests. The head is the lowest-numbered member whose final event has not been returned. So every request of member `i` is issued after the final event of member `i - 1` has been returned and before the final event of member `i`.
4. A Parked member has had no effect outside its own state. Whatever steps it took before its first host request, it would have taken alone.

So the sequence of events `advance` returns is the concatenation, in member order, of the event sequence each member produces when run alone. Services, and anything they keep per run, see exactly the serial order of requests.

What does differ: a member may execute its pure steps earlier in wall time than it would have serially. A test that reads a clock can observe that, as it can observe any change in speed today.

### 10.7 Window and memory

`window` bounds how many members a round considers. The default, 4,096, is above every break-even batch size in the [design measurements at the approved revision](https://github.com/Kayyo321/neura/blob/9325d45cedddb806185f8fce196ddc4179c767c4/docs/lang/GpuExecution.md#4-measurements). A fleet steps only the head when the width is 1.

Several members' heaps are alive at once. The memory rule of step 4: the head may always start; any other Waiting member may start only while the sum of `live_heap_cells()` over started, unfinished members is below `limits.max_heap_cells`. Started members are never suspended for memory, and each still traps at its own `max_heap_cells`, so the fleet's total can exceed one member's limit by what started members go on to allocate. In the worst case that is `window` times the limit; an embedder that cannot afford this passes a smaller window.

### 10.8 Callbacks

A host service may call back into the program while a request is in progress (`CallbackContext` in `src/nvm/host.hpp`). In a fleet this can only happen for the head, while it is Awaiting. The callback runs to completion inside the head's `Execution::invoke_callback`, one step at a time, through the private `step`. A member's `RunLimits` carry no device, so those steps use the reference evaluator. They are not part of any round.

### 10.9 `run_fleet`

```cpp
using MemberFinished = std::function<void(std::size_t member, const RunResult& result)>;

// Runs every member to its end, issuing each host request to its bound
// service. Calls `finished` for each member, in member order, as soon as its
// final event is returned and before any later member's request is issued.
void run_fleet(Fleet& fleet, const Bindings& bindings, const MemberFinished& finished);
```

It loops on `fleet.advance()`. For a `HostRequest` it invokes the bound service with a `CallbackContext` whose `call` forwards to `fleet.invoke_callback` and serves nested requests the same way, counts the request and every nested one for that member as `nvm::run` does, and calls `fleet.complete`. For a final event it sets `result.host_requests` and calls `finished`.

### 10.10 `neura test`

In `off`, `run_tests` in `src/cli/main.cpp` is unchanged. In `auto` and `always`, when a device is in use (sections 11.4 and 14.3), it:

1. builds and loads the test image as today and leaves `image.entry` alone;
2. creates a `Fleet` with `limits = options.limits`, the mode's device, and the default window, and adds one member per test, in test order, with `add(i, {})`, where `i` is the test's start function index, the value `run_tests` assigns to `image.entry` today;
3. calls `run_fleet` with a `finished` callback that does, for member `i`, exactly what the body of today's loop does after `nvm::run` returns: `host.services.end_run()`, then the `PASS` or `FAIL` line for test `i` from the result.

Because a test's console writes are host requests, they are issued only while that test is the head, after the previous test's line has been printed. The byte stream on standard output, the files and sockets each test sees, the summary line, and the exit status are those of the serial runner. A test that writes early simply parks until it is the head.

When no device is in use, `auto` and `always` run the tests one at a time, as `off` does.

### 10.11 `neura run --each`

```sh
neura run [options] --each <lists> <file.nvr> [-- common arguments...]
```

The image runs once per argument list in the file `<lists>`. Each run is a member; a member's arguments are the common arguments after `--`, if any, followed by its list's arguments. This is the command that gives one program enough simultaneous work for a device to help: for example one image applied to every line of an input set.

**The file.** It is read as bytes, in full, before anything runs.

- It is split into lines at LF. Text after the last LF is a line only when it is not empty.
- A line whose first byte is `#` is a comment and produces no member. Every other line produces one member, including an empty line, which gives a member no arguments of its own.
- A line is a sequence of arguments separated by one or more spaces or tabs. Spaces and tabs at the start and end of a line are ignored.
- A **bare** argument is one or more bytes, none of them a space, a tab, `"`, or `\`. It is taken literally.
- A **quoted** argument starts and ends with `"`. Between the quotes, `\"`, `\\`, `\n`, `\r`, `\t`, and `\xHH` with two hexadecimal digits stand for one byte each, as in a [host trace](../runtime/NVM.md#determinism-limits-and-observability); every other byte except `"` and `\` is taken literally. `""` is an empty argument. A quoted argument must be followed by a space, a tab, or the end of the line.
- Every argument must be valid UTF-8 after unescaping.

Errors in the file are reported as `neura: error: <lists>: line <n>: <text>` with status 1, before any member runs:

| Case | Text |
| --- | --- |
| A line ends with CR | `carriage return before the line end` |
| `"` or `\` inside a bare argument | `quote or backslash in an unquoted argument` |
| A quoted argument is not closed | `unterminated quoted argument` |
| A backslash sequence other than the six listed | `invalid escape` |
| A quoted argument is followed by another byte | `expected a space after a quoted argument` |
| An argument is not valid UTF-8 | `argument <k> is not valid UTF-8`, counting from 1 within the line |

Without a line number: `<lists>: no argument lists` when the file produces no member, `<lists>: more than 1048576 argument lists`, and `<lists>: larger than 64 MiB`.

**Running.** The image is loaded, validated, and bound once. Capabilities and run limits apply to every member separately. Section 14.3 says when the members run as a fleet. In a fleet they are added in file order with `Fleet::add(image.entry, arguments)` and driven by `run_fleet`; otherwise each is run in file order with `nvm::run`. Either way, when a member finishes, and before any later member's host request is issued, the runner calls `host.services.end_run()`, as `neura test` does after each test, and then reports the member:

| Member ended with | Standard error |
| --- | --- |
| Status 0 | Nothing |
| Another status | `<file.nvr>: <lists>:<n>: exit status <s>` |
| A trap | `<file.nvr>: <lists>:<n>: trap: <diagnostic>`, with the diagnostic `nvm::describe` gives today |

`<n>` is the member's line number in the file. The exit status of the command is the status of the first member, in file order, that did not end with status 0, where a trapped member's status is `nvm::exit_status` of its trap; it is 0 when every member ended with status 0. Every member runs whatever earlier members did.

**What it is equivalent to.** By section 10.6, standard output and every host effect are those of running the members one after another in file order in one process. Compared with a shell loop of separate `neura run` commands, the differences are that the image is loaded once, the members share the process's standard input and host providers (reset between members by `end_run`), the report lines above replace each command's own trap line, and there is one exit status.

**Other options.** `--stats` prints, in place of the usual `stats:` line, `stats: members <m>, steps <sum over members>, peak heap cells <largest over members>, host requests <sum over members>, <image costs>, image bytes <n>`, followed by the `gpu:` line. `--each` cannot be combined with `--record`, `--replay`, or `--profile` (`--each cannot be combined with <option>`), may be given once (`--each given more than once`), and applies only to `run` (`--each applies only to run`).

**What gains.** Members are batched while they compute and park when they make a host call until earlier members have finished. A program that reads its arguments, computes, and prints at the end keeps many members in the same rounds. A program that prints as it goes runs mostly one member at a time, with the same results.

### 10.12 The cost of a fleet whose jobs stay on the CPU

A round whose jobs are all declined does the same work per step as a single execution, through the same `begin_step`, `evaluate_outputs`, and `finish_step`. What it adds is the job list and moving between members' state. This plan assumes that is small and does not rely on the assumption: phase 2 measures it (section 19), and auto mode stops forming rounds, by returning a `batch_hint` of 1, once it has rejected the device.

## 11. Auto mode

### 11.1 What it decides

Auto mode makes three decisions, each by predicted time only.

| Decision | When | Deterministic |
| --- | --- | --- |
| Whether to open a device at all | Before the Vulkan loader is touched, from the image and the jobs seen so far (section 11.5) | Yes |
| Whether the opened device is worth keeping | Once, right after it is opened and measured (sections 11.6 and 11.7) | No: it uses measured time |
| Which jobs of each call go to the device | Every call after that (sections 11.8 and 11.9) | No: it uses measured time |

It is implemented as one `GraphDevice`, `AutoDevice`, in `src/nvm/placement.hpp` and `placement.cpp` (library `neura_nvm`). Executions and fleets treat it like any other device: they offer jobs, it evaluates some and declines the rest, and declined jobs are evaluated by reference. Nothing outside `AutoDevice` knows about placement.

### 11.2 The cost model

All times are in microseconds as `double`. For a graph `g`, section 7.5 gives its layers `L`, critical work `C`, edges `E`, nodes `N`, and I/O words `W`.

```text
cpu(g)   = c0 + c1 × E + c2 × N                 reference evaluation of one job
crit(g)  = b0 × L + b1 × C                      time one request needs on an idle device
each(g)  = crit(g) / K + d × W                  what one more request of g adds to a busy dispatch
gpu(S)   = a + max over g in S of crit(g) + sum over jobs j in S of each(g_j)
```

`S` is the set of jobs sent in one call. `a` is the fixed round trip; `b0` is the cost of a layer; `b1` is the cost of one edge on the longest chain of work in a layer; `K` is how many requests the device runs side by side; `d` is the host cost of one word of input or output. `CpuModel` holds `c0, c1, c2`, and `DeviceModel` holds `a, b0, b1, K, d`.

### 11.3 Constants

| Name | Value | Use |
| --- | --- | --- |
| `margin` | 0.75 | The device is chosen only when `gpu` is at most `margin` times the CPU time of the same jobs. |
| `open_budget` | 150,000 | Predicted saving, in microseconds, that must accumulate before a device is opened. About the cost of opening and calibrating one. |
| `give_up_work` | 3,000,000 | Predicted CPU time, in microseconds, after which a workload that has not reached `open_budget` is rejected: it could save under 5 percent. |
| Optimistic device | `a` 5, `b0` 0.17, `b1` 0.07, `K` 4000, `d` 0.00013 | Used before a device is opened. Half the costs measured for the RTX 5080 in the [design measurements at the approved revision](https://github.com/Kayyo321/neura/blob/9325d45cedddb806185f8fce196ddc4179c767c4/docs/lang/GpuExecution.md#4-measurements). |
| Nominal CPU | `c0` 0.08, `c1` 0.0008, `c2` 0.002 | Used before a device is opened. Twice the costs fitted to the CPU column of the [design measurements at the approved revision](https://github.com/Kayyo321/neura/blob/9325d45cedddb806185f8fce196ddc4179c767c4/docs/lang/GpuExecution.md#4-measurements). |
| Correction | starts at 1; moves one eighth of the way per dispatch; one sample may differ from the current value by at most a factor of 2; kept within 1/16 and 64 | Section 11.9. |

The optimistic device and nominal CPU are deliberately generous to the GPU. They decide only whether opening a device could possibly pay; once a device is open, its own measurements replace them.

### 11.4 `AutoDevice`

```cpp
struct Opened {
    std::unique_ptr<GraphDevice> device;     // owns whatever it needs
    DeviceModel device_model;
    CpuModel cpu_model;
    std::string name;
};
// Opens and measures a device, or returns nothing and sets `reason`.
using Opener = std::function<std::optional<Opened>(std::string& reason)>;

struct AutoOptions {
    std::size_t max_jobs = 1;                // the most jobs one call will carry
    std::function<double()> now;             // microseconds; defaults to the steady clock
};

class AutoDevice final : public GraphDevice {
public:
    AutoDevice(const nvr::Image& image, Opener opener, AutoOptions options = {});
    enum class State { Gated, Active, Rejected };
    State state() const;
    const std::string& reason() const;       // why it is Rejected
    const std::string& device_name() const;  // empty until opened
    double correction() const;
    // GraphDevice
};
```

| State | `evaluate` | `batch_hint` |
| --- | --- | --- |
| Gated | Declines every job and does the accounting of section 11.5. | `max_jobs` |
| Active | Places the call's jobs (section 11.8). | `max_jobs` |
| Rejected | Declines every job. Does no accounting. | 1 |

A job that `AutoDevice` declines by its own choice is counted in `declined_placement`. The other counters are the inner device's.

An inner device can publish `graph_shapes()` and `failure_reason()`. The verdict and active placement use those shapes when supplied, including the device's table limit and later demotions. Otherwise they use the image's static shapes. Jobs omitted by this wrapper count as placement declines. Auto retains the last shapes and counters after rejection, including when an execution continues to offer jobs.

The CLI passes `max_jobs = 1` for `neura run` and the smaller of the test count and the fleet window for `neura test`. Its opener creates the `gpu::Context`, runs the calibration of section 11.6, and returns a `VulkanGraphDevice` for the image; the returned device keeps the context alive.

### 11.5 Before a device is opened

**Prospect, at construction.** Using the optimistic device and the nominal CPU, the constructor tests every eligible graph:

```text
max_jobs × (margin × cpu(g) − each(g)) > a + crit(g)
```

If no graph passes, the state is Rejected at once with the reason `no graph can gain at this batch size`. This is a pure function of the image and `max_jobs`. For `neura run` it means: unless the image holds a graph that even the optimistic device would evaluate faster on its own, no device is created, the CLI passes no device to the execution, and the run is the `off` run with no added work per step.

**Gate, per call, while Gated.** For the jobs of the call, compute the best gain of section 11.8 with the optimistic device, the nominal CPU, and a correction of 1; add it to `saved`. Add `cpu(g)` under the nominal CPU, for every job of the call, to `work`. Then:

- if `saved ≥ open_budget`, call the opener. On failure the state is Rejected with `unavailable: <reason>`. On success apply the verdict of section 11.7.
- otherwise, if `work ≥ give_up_work`, the state is Rejected with `the workload cannot gain`.

The jobs of the call that opens the device are declined like the others. Placement starts with the next call. For the same image, batch limit, and sequence of jobs, the moment a device is opened, or the workload is given up on, is the same on every machine and every run. The gate groups jobs by graph and sums CPU work in first-occurrence order. It sums gain in the critical-time and graph-index order of section 11.8. The calculation uses binary64 with the default rounding mode and explicit fused multiply-add operations so compiler contraction does not change the result.

Opening is synchronous. Its cost, 40 to 130 ms in the [design measurements at the approved revision](https://github.com/Kayyo321/neura/blob/9325d45cedddb806185f8fce196ddc4179c767c4/docs/lang/GpuExecution.md#4-measurements) plus the calibration of section 11.6, is paid at most once, after the workload has shown that a good device could have saved about that much already.

### 11.6 Calibration

Calibration measures `DeviceModel` and `CpuModel`. `placement.hpp` exposes `probe_image()`, an `nvr::Image` whose graph table is the four probe graphs below. The opener creates a temporary `VulkanGraphDevice` for that image, measures, and destroys it before creating the device for the program's image.

| Probe | Input lanes | Neurons | Shape (L / E / N / C / W) |
| --- | --- | --- | --- |
| A, unit | 1 | One identity neuron reading the lane with weight 1. Output: the neuron. | 1 / 1 / 1 / 1 / 7 |
| B, chain | 1 | 512 identity neurons; the first reads the lane, each next one reads the previous neuron, weight 1. Output: the last neuron. | 512 / 512 / 512 / 512 / 7 |
| C, fat chain | 64 | 512 ReLU neurons. The first reads lanes 0 to 63, lane 0 with weight 1 and the rest with weight 0. Each next one reads lanes 1 to 63 with weight 0 and the previous neuron with weight 1. Output: the last neuron. | 512 / 32,768 / 512 / 32,768 / 70 |
| D, io | 256 | 256 identity neurons; neuron `i` reads lane `i` with weight 1. Outputs: all 256 neurons in order. | 1 / 256 / 256 / 4 / 517 |

All input lanes are 1. A device measurement `m(probe, R)` is the wall time of one `evaluate` call carrying `R` jobs of that probe: three unmeasured calls, then five measured ones, and the median of the five. The six measurements, in this order, are `m(A,1)`, `m(B,1)`, `m(C,1)`, `m(D,1)`, `m(B,1024)`, `m(D,1024)`. Then:

```text
a   = m(A,1)
b1  = max(0, (m(C,1) − m(B,1)) / 32256)
b0  = max(0, (m(B,1) − a) / 512 − b1)
K   = clamp(1024 × 512 × (b0 + b1) / max(m(B,1024) − m(B,1), 0.001), 1, 65536)
d   = max(0, ((m(D,1024) − m(D,1)) / 1024 − (b0 + 4 × b1) / K) / 517)
```

A CPU measurement `t(probe)` is the wall time of 256 consecutive `evaluate_outputs` calls on that probe divided by 256: five such measurements and their median. Then:

```text
c1  = max(0, (t(C) − t(B)) / 32256)
c2  = max(0, (t(B) − t(A)) / 511 − c1)
c0  = max(0, t(A) − c1 − c2)
```

Every probe job must come back evaluated with the reference outputs; if one does not, or any measurement is not finite, or `a` exceeds 10,000, the opener fails with `calibration failed`. Calibration results are not stored anywhere outside the process.

From the probe times in the [design measurements at the approved revision](https://github.com/Kayyo321/neura/blob/9325d45cedddb806185f8fce196ddc4179c767c4/docs/lang/GpuExecution.md#4-measurements), eight calls of each of the six measurements take about 45 ms on the discrete GPU, 90 ms on the integrated GPU, and 360 ms on llvmpipe. Most of it is probe C and the two 1,024-job measurements.

### 11.7 Verdict

With the measured models and `n = max_jobs`, the device is kept when some eligible graph satisfies

```text
n × (margin × cpu(g) − each(g)) > a + crit(g)
```

and the state becomes Active. Otherwise the inner device is destroyed and the state is Rejected with `the device cannot beat the CPU`. This test is exact for the model: if any mix of at most `n` jobs were predicted to gain, then `n` copies of the mix's most profitable graph would be too.

This is where a GPU that is present but too weak is dropped. In the [design measurements at the approved revision](https://github.com/Kayyo321/neura/blob/9325d45cedddb806185f8fce196ddc4179c767c4/docs/lang/GpuExecution.md#4-measurements), it discards llvmpipe for every measured graph, and keeps the integrated GPU only for an image that has a wide, shallow graph and a fleet of at least 58.

### 11.8 Placing the jobs of a call

Let `s` be the correction of section 11.9. For the call's jobs:

1. Count the jobs per graph. Jobs for ineligible graphs are declined.
2. A graph is a **candidate** when `margin × cpu(g) − s × each(g) > 0`. Jobs of other graphs are declined: adding them to any dispatch costs more than evaluating them on the CPU.
3. Sort the candidates by `crit(g)` ascending, then by graph index.
4. Walk the sorted list keeping a running sum. After adding the `k`-th candidate, with `n_i` jobs each:

   ```text
   gain(k) = Σ over the first k candidates of n_i × (margin × cpu(g_i) − s × each(g_i))  −  s × (a + crit(g_k))
   ```

5. Take the `k` with the largest `gain(k)`; on a tie the smallest `k`. If that gain is not above zero, decline every job. Otherwise send every job of the first `k` candidates to the inner device in one `evaluate` call, in the order the jobs were given, and decline the rest.

The sorted walk is what keeps a deep graph from holding up a batch: a graph is included only if the jobs up to and including it repay the longest wait among them. A call with one job is the same procedure with one count. The work per call is linear in the number of jobs plus a sort of the distinct graphs in the call.

Jobs the inner device declines, because they escaped or it failed, are returned declined. If the inner device has failed, the state becomes Rejected with `device failed: <reason>`.

### 11.9 Correction

The model is approximate, devices warm up and throttle, and other processes use them. After each inner `evaluate` call, with `elapsed` its wall time from `options.now` and `predicted = a + crit(g_k) + Σ n_i × each(g_i)` for the jobs sent:

```text
sample = clamp(elapsed / predicted, s / 2, 2 × s)
s      = clamp(s + (sample − s) / 8, 1/16, 64)
```

A device that is slower than its calibration raises `s`, which shrinks the set of candidates and raises the batch a dispatch needs; a faster one lowers it. One slow dispatch moves `s` by at most 12.5 percent. If `s` rises until nothing is placed, nothing further is measured and the device stays unused; that is the intended outcome when it keeps losing.

When both the predicted and elapsed times are zero, the sample supplies no timing evidence and leaves `s` unchanged.

### 11.10 What auto mode will choose for today's programs

From the [design measurements at the approved revision](https://github.com/Kayyo321/neura/blob/9325d45cedddb806185f8fce196ddc4179c767c4/docs/lang/GpuExecution.md#4-measurements):

- **`neura run`.** No compiled graph measured passes the prospect test with `max_jobs = 1`: the best case, the 499-neuron graph, would need about 10 microseconds on the optimistic device against a nominal 2.2 on the CPU. No device is opened and the run costs what `--no-gpu` costs.
- **`neura test` with few tests.** A fleet forms when the image has a graph that passes the prospect test at the test count: from about seven tests for an image with a wide, shallow graph like the 499-neuron one, and from about thirty for the other graphs measured. The gate then opens a device only if enough tests are at profitable graphs in the same rounds; otherwise the workload is rejected after `give_up_work` and the rest runs one test at a time.
- **`neura run --each` with thousands of argument lists.** This is the fleet case below whenever the program computes before it prints.
- **Fleets of hundreds or thousands of members on a discrete GPU.** The gate opens within tens of rounds, the verdict keeps the device, and placement sends most rounds' jobs to it.
- **An integrated or software GPU.** The verdict rejects it for the graphs measured, apart from the wide, shallow case above.

The implementation measurements in section 4 are observed results on the release host.

## 12. The Vulkan device

New library `neura_gpu` in `src/gpu/`, namespace `neura::gpu`, linking `neura_nvm`. It is built only when the build enables it (section 13).

| File | Contents |
| --- | --- |
| `src/gpu/vulkan_api.hpp`, `.cpp` | Opens the loader and holds the function pointers. |
| `src/gpu/device.hpp`, `.cpp` | `Context` and `VulkanGraphDevice`. |
| `src/gpu/context_internal.hpp`, `context.cpp` | Private Vulkan context state and candidate selection. |
| `src/gpu/calibrate.hpp`, `.cpp` | The measurements of section 11.6 and the opener the CLI gives to `AutoDevice`. |
| `src/gpu/graph_eval.comp` | The kernel of section 8. |

### 12.1 Loader

`neura_gpu` compiles with `VK_NO_PROTOTYPES` and does not link the Vulkan library. `Context::create` opens it:

- Linux: `dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL)`.
- Windows: `LoadLibraryExA("vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)`.

It resolves `vkGetInstanceProcAddr` from the library and every other function through `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr`. A missing library or symbol makes the GPU unavailable with reason `Vulkan loader not found`. The library is not opened in `off`, and in `auto` not before the gate of section 11.5 opens.

### 12.2 `Context`

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
4. For each candidate in order, create the logical device with one queue and with `robustBufferAccess` enabled when the device reports it; create the shader module, a descriptor set layout with three `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` bindings for the compute stage, the pipeline layout, the compute pipeline, a command pool with `VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT`, and one fence; then run the self-test of section 12.5. The first candidate that passes is the device. A candidate that fails any step is destroyed and the next is tried. If all fail, the reason is `<device name>: <failure>` for the first candidate.

### 12.3 `VulkanGraphDevice`

`VulkanGraphDevice(Context&, const nvr::Image&)` implements `GraphDevice`. Construction runs the eligibility check, packs `TABLES` under the device's limits, uploads it once through a host-visible staging buffer and `vkCmdCopyBuffer`, and allocates `IO` and `WIRES` at their initial capacities.

| Buffer | Memory | Usage |
| --- | --- | --- |
| `TABLES` | `DEVICE_LOCAL` | Storage, transfer destination |
| `IO` | `HOST_VISIBLE` and `HOST_COHERENT`, preferring a type that is also `HOST_CACHED`; mapped for the buffer's life | Storage |
| `WIRES` | `DEVICE_LOCAL` | Storage |

If no memory type satisfies a row, or an allocation fails, construction leaves the device in the failed state of section 9.5 with the reason `cannot allocate device memory`. A context serves one `VulkanGraphDevice` at a time. In `always` the CLI creates one for the program's image; the opener of section 11.4 creates one for the probe image, destroys it, and then creates one for the program's image. `batch_hint` returns the largest `std::size_t`.

The storage-buffer range further bounds `IO` and `WIRES`, whose ceiling is 256 MiB. Growth uses the largest power of two within both limits. An empty table binds a single dummy word without uploading records. A staging allocation without coherent memory is flushed before submission.

### 12.4 Dispatch protocol

For each dispatch:

1. Write the request records, input regions, and zeroed status words into the mapped `IO` buffer.
2. Submit a command buffer that binds the pipeline and the descriptor set and calls `vkCmdDispatch(R, 1, 1)`, with the fence. For `R = 1` the device reuses one command buffer recorded once; it is recorded again whenever a buffer is recreated, after the descriptor set is updated. For `R > 1` the command buffer is recorded for that dispatch.
3. Wait: poll `vkGetFenceStatus` for up to 2 ms, then call `vkWaitForFences` in 100 ms slices until the fence signals or 30 s have passed in total. Reset the fence.
4. Read each status word. For status 2, copy the output words to the job's `outputs`, sign-extending each from 32 bits, and set `evaluated`. For status 3, decline the job, count it in `escaped`, and demote its graph. Any other value fails the device.

Jobs for ineligible graphs are declined before a dispatch is built and never reach the device. Buffers grow only between dispatches. The steady clock used in step 3 serves only the wait; no run result depends on it.

After a timeout the device is not idle, so its Vulkan objects must not be destroyed. A device that failed by timeout skips all teardown and leaves its resources to process exit. Any other device waits for idle and destroys its objects in reverse creation order.

A dispatch counts after a successful queue submission. The device checks all status words in the dispatch before publishing any outputs, so an invalid status declines that dispatch and the remaining jobs. Failure reasons include `invalid dispatch status <n>`, `dispatch timed out after 30 seconds`, and `vk<operation> failed (<result>)`.

### 12.5 Self-test

`layered.hpp` exposes `self_test_graphs()`, a fixed list of small graphs with input sets, built in code. `Context::create` packs them, dispatches every request in one call, and compares every output and status with `evaluate_graph`. Expected values are computed at run time, never stored. The list must contain:

1. `relu(a + b - 1)` on all four inputs.
2. An identity neuron and a ReLU neuron with negative weights and a nonzero bias, on inputs that make the ReLU clamp and not clamp.
3. A chain of 200 neurons, each `relu(1 - previous)`, so that 200 layers and barriers execute.
4. One layer of 200 neurons, each reading a different input lane, followed by one identity neuron that sums all 200, so that a layer wider than a work group and a fan-in above 64 execute.
5. A graph whose output list repeats a wire and names an input wire.
6. An identity neuron computing `40000 × a`, which must return status 3 for `a = 1` and status 2 for `a = 0`.

The dispatch must hold at least 128 requests across these graphs. Any difference rejects the candidate with `self-test failed`. The same list runs against `ModelDevice` in the test suite.

The implemented self-test uses the following input patterns. The list uses all four bit pairs for each of the first two graphs and both bit values for the chain, repeated output, and escape graphs. The wide graph uses all-zero and all-one inputs plus 112 patterns. For pattern `s` from 1 to 112, lane `j` is set when `(67j + s) mod 200 < s`.

## 13. Libraries and build

| Library | Change |
| --- | --- |
| `neura_nvm` | Adds `graph_device.cpp`, `layered.cpp`, `fleet.cpp`, and `placement.cpp`. Still links only `neura_nvr` and its existing platform libraries. No Vulkan dependency. |
| `neura_gpu` | New. Built when the GPU is enabled. Links `neura_nvm` and `${CMAKE_DL_LIBS}`. |
| `neura` | Links `neura_gpu` and is compiled with `NEURA_HAS_GPU=1` when the GPU is enabled. |

`CMakeLists.txt`:

- `set(NEURA_GPU "AUTO" CACHE STRING "GPU graph execution: AUTO, ON, or OFF")`. This CMake cache variable and the run-time environment variable of section 14.2 share a name and are unrelated.
- Locate the header with `find_path(NEURA_VULKAN_INCLUDE_DIR vulkan/vulkan.h)` and the compiler with `find_program(NEURA_GLSLANG glslangValidator)`.
- `AUTO` enables the GPU when both are found and the platform is not Apple, and otherwise disables it with a `message(STATUS ...)` naming what was missing. `ON` fails configuration with `message(FATAL_ERROR ...)` when either is missing. `OFF` disables it.
- The shader is compiled at build time with `glslangValidator -V --target-env vulkan1.0 -x -o <build dir>/generated/graph_eval.spv.inc src/gpu/graph_eval.comp` through `add_custom_command`, and `device.cpp` includes the result, a comma-separated list of 32-bit words, as the initializer of a `std::uint32_t` array. No generated file is checked in.

Fleets, placement, `ModelDevice`, and `AutoDevice` are in `neura_nvm` and are built and tested in every configuration. A runtime-only distribution that wants the GPU ships `neura_nvm` and `neura_gpu`; one that does not ships `neura_nvm` alone.

## 14. Command line

### 14.1 Options

These apply to `run` and `test` and are rejected on other commands with `<option> applies only to run and test`.

| Option | Meaning |
| --- | --- |
| `--gpu=auto` | The default. The runtime decides, as section 11 specifies. |
| `--gpu=always` | Every eligible graph goes to a GPU when one is usable; otherwise the reference evaluator, silently. |
| `--gpu=off`, `--no-gpu` | The reference evaluator only. The Vulkan loader is not opened and no fleet is formed. |
| `--require-gpu` | Selects `always`, and fails before entry unless a GPU device is created. |
| `--gpu-software` | Also accept software Vulkan implementations, ranked last. |
| `--gpu-verify` | Check every device evaluation against the reference evaluator and stop at the first difference. |

Usage errors:

| Case | Text |
| --- | --- |
| Two mode options that name different modes, counting `--no-gpu` as `off` | `conflicting GPU modes` |
| `--require-gpu` with `--gpu=auto`, `--gpu=off`, or `--no-gpu` | `--require-gpu needs --gpu=always` |
| `--gpu-software` or `--gpu-verify` with `--gpu=off` or `--no-gpu` | `<option> cannot be combined with --gpu=off` |
| `--gpu=` with any other value | `expected --gpu=auto, --gpu=always, or --gpu=off` |

### 14.2 Environment

`NEURA_GPU` set to `auto`, `always`, or `off` selects the mode when the command line has none of `--gpu=`, `--no-gpu`, and `--require-gpu`. A mode option on the command line overrides it. An unset or empty variable has no effect. Any other value is the error `NEURA_GPU must be auto, always, or off`. The variable is read only by `run` and `test`.

### 14.3 Order of work

`run` without `--each`, after the image is loaded and validated and before imports are bound or a replay trace is read:

| Mode | Device given to the execution through `options.limits.graph_device` |
| --- | --- |
| `off` | None. |
| `always` | Create the context and a `VulkanGraphDevice`. None if that fails, unless `--require-gpu` makes it an error. |
| `auto` | Construct an `AutoDevice` with `max_jobs = 1` and the opener of section 11.4. None if its state is Rejected. |

`test`, after the image is built and loaded:

| Mode | How the tests run |
| --- | --- |
| `off` | One at a time, as today. |
| `always` | Create the context and a `VulkanGraphDevice`. If that fails, one at a time, unless `--require-gpu` makes it an error. Otherwise as a fleet with that device (section 10.10). |
| `auto` | Construct an `AutoDevice` with `max_jobs` equal to the smaller of the test count and 4,096. If its state is Rejected, one at a time. Otherwise as a fleet with that device. |

`run --each` follows the `test` table with members in place of tests: `max_jobs` is the smaller of the member count and 4,096, and "one at a time" means `nvm::run` per member in file order (section 10.11).

Under `--gpu-verify` a device, when there is one, is wrapped in a `VerifyingDevice` before it is used. In a build without the GPU, `auto` and `always` behave as `off`. An invalid image still fails before any device or host service is touched.

### 14.4 Errors

All are printed as `neura: error: <text>` and exit with status 1.

| Case | Text |
| --- | --- |
| `--require-gpu` in a build without the GPU | `--require-gpu: this build has no GPU support` |
| `--require-gpu` and no device | `--require-gpu: <reason>` with the reason from section 12 |
| `--require-gpu` and the device failed during the run | `--require-gpu: the device failed during the run: <reason>`, printed after the run's own output; the exit status is 1 whatever the program returned |
| `--gpu-verify` found a difference | The `GraphDeviceMismatch` message |

Without `--require-gpu`, an unavailable, rejected, or failed device is not an error and prints nothing.

### 14.5 `--stats`

`--stats` prints one more line to standard error after its existing output (after the `stats:` line of `run`, including the form `run --each` prints, and once after the summary of `test`). The existing `stats:` line and the per-test figures on standard output do not change. The line is exactly one of:

```text
gpu: off
gpu: not built
gpu: always, unavailable (<reason>)
gpu: always, device "<name>", eligible graphs <E> of <G>, evaluated <n>, ineligible <n>, escaped <n>, failed <n>, dispatches <n>
gpu: auto, not used (<reason>)
gpu: auto, device "<name>", eligible graphs <E> of <G>, evaluated <n>, kept on cpu <n>, ineligible <n>, escaped <n>, failed <n>, dispatches <n>, correction <x.xx>
gpu: auto, device "<name>", eligible graphs <E> of <G>, evaluated <n>, kept on cpu <n>, ineligible <n>, escaped <n>, failed <n>, dispatches <n>, correction <x.xx>, rejected (<reason>)
```

The counters are the fields of `GraphDeviceStatistics`; `kept on cpu` is `declined_placement`. For `auto`, the reason is `AutoDevice::reason()`, or `the workload stayed under the opening budget` when the state is still Gated at the end. The last form is used when a device was opened and the state ended Rejected.

### 14.6 Usage text

`usage()` in `src/cli/main.cpp` adds `[--each <lists>]` to the `run` line, `[--gpu=auto|always|off] [--no-gpu] [--require-gpu] [--gpu-software] [--gpu-verify]` to the `run` and `test` lines, and these lines after the capability list:

```text
graph execution: run and test choose between the CPU and a GPU by predicted speed.
  --gpu=auto               decide automatically (default)
  --gpu=always             evaluate every eligible graph on a GPU when one is usable
  --gpu=off, --no-gpu      evaluate graphs on the CPU
  --require-gpu            --gpu=always, and fail unless a GPU device is available
  --gpu-software           also accept software Vulkan implementations
  --gpu-verify             check every GPU evaluation against the CPU
  --each <lists>           (run) run the image once per line of <lists>; each line is one argument list
```

## 15. Existing tests

`CMakeLists.txt` sets `NEURA_GPU=off` on the existing Neura CTest entries by iterating the directory's `TESTS` property. The two GPU entries are exempt. With CMake 3.28 or newer, the setting also applies to the libclang submodule's own tests, which do not invoke the Neura CLI. Those tests therefore keep running one execution at a time on the reference evaluator. No existing test script, fixture, expected output, or baseline should need to change: `--stats` is used only by `tests/stdlib/area.cmake`, which compares standard output, and `tests/benchmarks/run.cmake`, which extracts figures by name, so the added `gpu: off` line on standard error affects neither. `tests/benchmarks/baseline.txt` is unaffected because steps, heap cells, neurons, and image bytes do not depend on the evaluator. If an existing test does fail, the implementer reports it and does not edit the expectation.

## 16. Verification

Each entry names what it compares, over what domain, and what kind of evidence it is, in the terms of the [release checklist](../Release.md). `backend-layered`, `backend-fleet`, and `backend-placement` are new suites in `neura_backend_tests`; they and `backend-each` are built in every configuration.

| CTest | What it establishes | Kind |
| --- | --- | --- |
| `backend-layered` | Eligibility: a graph at the weight budget is accepted and one unit over is rejected; `INT64_MIN` weights and biases are rejected without overflow in the check; a graph without nodes is ineligible. Packing: every directory, node, edge, layer, and output word for hand-built graphs; layers respect every edge; `table_limit` skips a graph and keeps later ones. Shapes: `L`, `E`, `N`, `C`, and `W` for hand-built graphs, including a layer wider than 64. | Structural |
| `backend-layered` | `ModelDevice` against `evaluate_graph` for the integer circuits of `backend-circuits`: every pair of 8-bit operands for each operation, and the boundary and pseudo-random operands that suite uses for wider types. | Exhaustive (8-bit), sampled (wider) |
| `backend-layered` | Escape: the self-test graph 6 and a graph whose value is exactly `wire_bound` (accepted) and `wire_bound + 1` (escaped); demotion after the first escape; a graph whose reference evaluation overflows is never returned as evaluated. `self_test_graphs()` against `ModelDevice`. | Structural |
| `backend-layered` | The trap cases of `backend-vm` built from `logic_model()` (non-binary output lane, invalid selector, graph overflow), run with `ModelDevice` wrapped in `VerifyingDevice`: the trap kind and detail text equal the run without a device. The overflow graph must be ineligible by `weight_budget`; the non-binary graph must be evaluated by the device. | Structural |
| `backend-layered` | Every image of `image_corpus(environment)` in `tests/backend/fixtures.hpp`, run through `run_captured` without a device and with `ModelDevice` wrapped in `VerifyingDevice`: `completed`, `status`, the trap's kind, reason, detail, function, block, and callers, `steps`, `host_requests`, `peak_heap_cells`, and the captured output are equal. | End to end |
| `backend-fleet` | The step split: for every image of the corpus, a loop of `begin_step`, `evaluate_outputs`, and `finish_step` or `fail_step` produces the same events, in order, as `advance`. | End to end |
| `backend-fleet` | Equivalence: for every image of the corpus with an entry point, a fleet of eight members with different argument lists against eight `run_captured` runs in order: the sequence of host requests with their arguments, the captured output, and every `RunResult` field are equal. Repeated with no device, with `ModelDevice` wrapped in `VerifyingDevice`, and with windows 1, 3, and 4,096. | End to end |
| `backend-fleet` | Ordering and states, with hand-assembled images: a member that prints before an earlier member finishes parks, and the output is the serial concatenation; a member that traps, one that hits the step limit, and one that ends through a process exit completion; a host callback served for the head; the memory rule stops admission and the head still starts; `add` after `advance`, `advance` while a request is pending, and a device for another image throw. | Structural |
| `backend-fleet` | A sweep: 65,536 members created with `add_call` for a function of two 8-bit parameters from `tests/backend/fixtures`, against one `Execution` per input as `backend-conversion` runs them: every result and step count is equal. | Exhaustive (8-bit pairs) |
| `backend-each` (new CMake script `tests/backend/each.cmake`, built in every configuration) | The file grammar: bare and quoted arguments, every escape, an empty line, a comment, a final line without LF, and each error of section 10.11 with its exact text and line number, with no member run. With `tests/backend/arguments/first-argument.nra`: standard output equals the concatenation of separate `neura run` commands over the same lists; common arguments come first; a list that makes the program trap produces the report line and the trap's status as the exit status while later members still run; the exit-status rule for a nonzero status; the `stats:` form; the errors for `--record`, `--replay`, `--profile`, and a repeated `--each`. | End to end |
| `backend-placement` | `fit_device` and `fit_cpu`, the pure functions behind section 11.6: the six RTX 5080 measurements of section 4.6 (9.2, 253.9, 4605.6, 15.2, 371.9, 152.6) give `a` 9.2, `b0` 0.343, `b1` 0.135, `K` 2,123, `d` 0.000259 to three significant digits; the clamps apply for measurements that would give negative or unbounded values. | Structural |
| `backend-placement` | The placement procedure with fixed models and hand-computed expectations: all jobs declined below break-even; a deep graph left out of a batch of shallow ones and included once it has enough jobs; the tie rule; a gain of exactly zero declines; ineligible graphs declined. The prospect test, the gate opening at an exact call and identically on a second run, the give-up rule, and the verdict accepting the RTX 5080 parameters and rejecting the llvmpipe parameters of section 4.6 for the same shapes. | Structural |
| `backend-placement` | `AutoDevice` with an opener that returns `ModelDevice` and fixed models, and a scripted clock: state transitions, `batch_hint`, every counter, the correction after scripted elapsed times including both clamps, and rejection when the inner device fails. Then the corpus fleets of `backend-fleet` run through it wrapped in `VerifyingDevice`, with models that make it place some rounds and decline others: all results equal the serial runs. | Structural, end to end |
| `gpu-device` (new, built with the GPU) | `VulkanGraphDevice` against `evaluate_graph` with the same circuit domains as `backend-layered`, submitted in dispatches of up to 65,535 requests; a mixed dispatch over several graphs; a call large enough to split into several dispatches; `IO` and `WIRES` growth. Calibration returns finite, non-negative parameters and every probe job matches the reference. Uses `allow_software = true`. | Exhaustive (8-bit), sampled (wider) |
| `gpu-cli` (new CMake script) | For each program under `tests/backend/programs` and `tests/backend/traps` whose `--no-gpu --stats` run takes at most 200,000 steps: stdout, exit status, and stderr without the `gpu:` line are identical between `--no-gpu`, `--require-gpu --gpu-software --gpu-verify`, and `--gpu=auto --gpu-software --gpu-verify`. The same comparison for `neura test` on the suites under `tests/backend/testing`, and for `neura run --each` with a generated file of 2,000 argument lists for `first-argument.nra`. A trace recorded with `--no-gpu` replays with `--require-gpu --gpu-software`, and the reverse. The usage and run errors of section 14. `NEURA_GPU` and its precedence. Programs over the step bound are listed in the log. Only files declaring a `// run-options:` line are also compared with their fixture options. | End to end |

`gpu-device` and `gpu-cli` exit with status 77 and are registered with `SKIP_RETURN_CODE 77` when no Vulkan device, hardware or software, can be created, so CTest reports them as skipped and never as passed. Both carry the CTest label `gpu` and a timeout of 1,800 seconds.

**Limits of this evidence.** These are observed agreements over the stated domains. The argument of section 6.4 is a proof about the algorithm under the stated arithmetic, and section 10.6 is an argument about the fleet's algorithm; neither is a proof about a driver or about the implementation. A machine with only llvmpipe verifies the Vulkan path against a software implementation and no hardware driver. No test asserts which evaluator auto mode chose on a real device, because that depends on measured time; the placement logic is tested with fixed models, and on real devices only the equality of results is tested. A handoff must state which devices `gpu-device` and `gpu-cli` actually ran on, by the `gpu:` line, and name any that were skipped.

## 17. Documentation to update with the implementation

| Page | Change |
| --- | --- |
| [NVM](../runtime/NVM.md) | In "Graphs and continuations", after the profile paragraph: an implementation may compute a graph's output lanes by another method when the values equal the reference rule's and the reference rule does not overflow. A new section "Graph devices" with the contract of section 5. A new section "Fleets" with the rule of D13 and the equivalence of section 10.6. In "Determinism, limits, and observability", the sentence about the reference executor running one block at a time gains the fleet as a permitted faster executor. |
| [Backend](../Backend.md) | The directory table gains `src/gpu/` and the new `src/nvm/` files; "Running an image" gains the modes, the fallback rule, and the embedding interfaces; the test table gains the new entries. |
| [Tooling](Tooling.md) | `neura run --each` with its file format, report lines, and exit status (section 10.11); the options of section 14, the environment variable, the `gpu:` line, and the build requirement for `NEURA_GPU`. |
| [Testing](Testing.md) | "Tests run serially in declaration order" becomes: tests may be advanced together; each test's output, its host effects, and its report line appear in declaration order, exactly as when run one at a time. |
| [Status](Status.md) | The Tooling row lists `run --each` and the new options. |
| [Release](../Release.md) | Rows under "Toolchain and formats" mapping GPU graph execution, fleets, and auto placement to their tests and kinds. |
| [Project](../Project.md) | One sentence in "Runtime boundary": graph evaluation may run on a GPU, and independent executions may be advanced together, with results identical to the reference evaluator run one execution at a time. |
| `wiki/` | The matching pages, following [wiki maintenance](../Wiki.md). |
| This page | Becomes the specification: remove "plan", replace section 4 with the measurements a release actually records, and keep sections 5 to 14 as the contract. |

## 18. What to expect

The single-run, test, and argument-list measurements are in section 4. A forced device pays setup and dispatch costs for each eligible graph even when a CPU would be faster. Auto estimates those costs before opening the device and again before selecting jobs.

Further ways to use graph devices require separate design work:

1. **More consumers of fleets.** The decompiler's exhaustive lifting and the exhaustive suites of the toolchain's own tests, which today call the reference evaluator one input at a time.
2. **Shallower circuits.** Deep circuits take more work-group barriers. The cost model includes both the layer count and critical edge work. Parallel-prefix carries and tree reduction would trade more neurons for far fewer layers. That is an IRtoNeuronConversion change with its own cost contract; [Backend](../Backend.md#circuits) documents the ripple-carry adder and restoring division used today, and [Project](../Project.md#neurair-and-optimization) lists graph depth among the circuit costs.
3. **Parallelism in the language.** [Concurrency](ResourcesConcurrency.md) is deferred. A data-parallel construct would give one program many frames evaluating the same graph.

Also outside this specification: moving frames, exits, state operations, or the heap to the GPU; stepping fleet members on several CPU threads; any change to circuits, the image format, or the graph profile; device selection by the user, several devices at once, and pipeline caches; storing calibration between runs; opening a device on a background thread; macOS, and any API other than Vulkan.

## 19. Phases

Each phase is one reviewable change and leaves every existing test passing. Where a phase asks for a measurement, the handoff reports the command, the machine, and the numbers; a missed bound is reported and the constants of this plan are not changed to meet it.

| Phase | Work | Done when |
| --- | --- | --- |
| 1 | The step split of section 9.2, `graph_device.hpp`, `layered.*` with shapes, `ModelDevice`, `VerifyingDevice`, and the `RunLimits` field. No CLI change. | `backend-layered` and the step-split case of `backend-fleet` pass. All existing tests pass unchanged. `tests/benchmarks` wall time is within 3 percent of the previous commit. |
| 2 | The entry-point constructor, `Fleet`, and `run_fleet`. CLI: `neura run --each`, running its members one at a time. | `backend-fleet` and `backend-each` pass. Measured: the test files of three `tests/stdlib` areas run as a fleet without a device take at most 1.10 times as long as the same members run one at a time through `nvm::run`. |
| 3 | `NEURA_GPU` in CMake, `neura_gpu`, the kernel, `Context`, `VulkanGraphDevice`, and the self-test. CLI: every option of section 14.1 except `--gpu=auto`; the default is `off`. `neura test` and `neura run --each` use a fleet under `always`. | `gpu-device` passes on at least one named device. The `always` and `off` parts of `gpu-cli` pass. A build with `NEURA_GPU=OFF` passes all tests. |
| 4 | `placement.*`, `AutoDevice`, calibration, and `--gpu=auto`. The default is still `off`. | `backend-placement` passes. The `auto` parts of `gpu-cli` pass on at least one named device. |
| 5 | `auto` becomes the default; the `NEURA_GPU` environment variable; the environment setting on existing tests (section 15); the `gpu:` line; the documentation of section 17. | The full suite passes with the GPU built and with `NEURA_GPU=OFF`. Measured on a machine with a discrete GPU: (a) `neura run --stats` for every program of `tests/benchmarks` prints `gpu: auto, not used` and takes at most 1.05 times its `--no-gpu` wall time; (b) `neura test` over three `tests/stdlib` areas takes at most 1.10 times its `--no-gpu` wall time; (c) `neura run --each` with 65,536 argument lists, for a new fixture `tests/backend/each/collatz.nra` that parses one integer argument, counts its Collatz steps in `int64` arithmetic, and prints the count, is not slower with `--gpu=auto` than with `--no-gpu`; both wall times and the `gpu:` line are reported. The handoff lists the devices used and any skipped entry. |

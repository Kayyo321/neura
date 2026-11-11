# Decompiler

The decompiler reads an `.nvr` image with the shared validating loader and recovers what the image does from its executable content: its graphs, weights, types, layouts, globals, functions, blocks, exits, and host imports. It has three commands:

```sh
neura disassemble program.nvr -o program.nvra   # lossless text form
neura assemble program.nvra -o program.nvr      # back to an image
neura decompile program.nvr                     # typed reconstruction
```

Optional metadata, the source map (`SMAP`) and names (`NAME`), only labels the output. It never decides what an operation is. `decompile --no-provenance` omits it, and decompiling an image with both sections removed produces the same text as `--no-provenance`.

## Assembly: the lossless form

`disassemble` writes every record of every table in table order, each with its index, so the text names every field the encoding holds:

```text
nvr 1.0 profile 1
entry 0

type 0 text
type 1 array 0
type 2 signed 32
constant 0 type 0 bytes 48656c6c6f ; "Hello"

graph 1 inputs 2 outputs [ 2 ]
  node 0 relu -1 [ 0*1 1*1 ]

import 0 "neura.console.write_line" version 1 parameters [ 0 ] success 4 error 3

function 0 parameters 1 result 2 frame [ 1 3 ]
  block 0 graph 0 selector 0 inputs [ ]
    exit host import 0 arguments [ constant:0:0 ] result - resume 1 error 1 error-resume 2 assign [ ]

source-map
location 0 0 "hello-world.nra" 5 9
section "NAME" 0300000000...
```

| Record | Fields |
| --- | --- |
| `nvr 1.M profile P` | Header: the minor version the image declares and the graph profile. Must be first. |
| `entry F` | The entry function. |
| `type I KIND [WIDTH or REFERENCE]` | Kinds `void bool unsigned signed text bytes object array list float bfloat`. |
| `layout I parent P\|- fields [ T... ]` | Parent layout and field types, inherited fields first. |
| `constant I type T bytes HEX` | Payload bytes in hex, `-` when empty. A comment after `;` shows the value. |
| `global I type T initializer C\|-` | |
| `graph I inputs N outputs [ W... ]` | Followed by its nodes. |
| `node I identity\|relu BIAS [ SOURCE*WEIGHT... ]` | One neuron of the preceding graph. |
| `import I "NAME" version V parameters [ T... ] success T error T` | |
| `function I parameters N result T frame [ T... ]` | Followed by its blocks. |
| `block I graph G selector S inputs [ VALUE... ]` | Followed by its exits. |
| `exit KIND FIELDS assign [ SLOT = VALUE... ]` | Kinds `continue call return state host trap`, with the fields of [NVR](runtime/NVR.md#graphs-functions-and-exits). State operations are named, such as `read-field` and `read-version`. |
| `source-map`, then `location F B "PATH" LINE COLUMN` | The optional source map. |
| `section "TAG" HEX` | An optional section the loader does not interpret, such as `NAME`. |

A value is `slot:TYPE:INDEX`, `constant:TYPE:INDEX`, or `lanes:TYPE:FIRST-LANE`. Comments start with `;`.

`assemble` parses the text, checks the syntax and that each record's index is the next one in its table, and reports errors with their line number. It then encodes the image and loads it with the same loader as `neura run`, so an image that violates the format contract is rejected with the loader's diagnostic. The encoding of a valid image is canonical, and the loader keeps every byte of optional sections, so assembling the disassembly of any valid image reproduces it byte for byte. Editing the text is how tests construct images with changed weights or tables.

## Reconstruction form

`decompile` prints the image's imports, callbacks, native library, layouts, and globals, then each function's frame and blocks. A native library is shown as an opaque region with its target, size, and symbols: native code is not lifted. The lossless form writes it as a `native-library` record with the object in hexadecimal and one `native-symbol` record per symbol, and each callback as a `callback` record. Each block is labeled with how its meaning was established:

| Label | Meaning |
| --- | --- |
| `[decoded]` | The block's graph computes nothing: its exits read slots and constants. Everything shown is read from the image tables. |
| `[lifted: structural]` | The graph computes the operations shown. Proven by regeneration: building the block again from the shown operations with the compiler's own circuit library produces a graph identical to the image's, neuron for neuron and weight for weight, and identical exits. |
| `[lifted: exhaustive]` | No structural match, but the shown operations agree with the graph on every input. Used only for blocks with at most 20 input lanes. |
| `[circuit]` | Not lifted. The block's graph, its inputs, and its exits are shown, and the values its exits read are shown as output lanes. |
| `[reconstructed: …]` | Structure recovered from the block graph rather than read from a table: loop headers (blocks that are the target of an edge from a block they dominate) and the pairing of a scope-enter exit with its scope-exit (`[reconstructed: scope bN.eM]`; `[scope pair unresolved]` when no pair is found). |

Exits are always decoded: `goto bN`, calls, returns, state operations (`new L0`, `s1.field2`, `version(s3)`, `append(s2, v)`), host calls by import name and version, and traps with their reasons. A lifted block shows the operations that feed its exits as expressions over its inputs (`(s4 + 1)`, `((s2 % 2) == 0)`, `(s1 as float64)`), the condition that selects between two exits as `if … then … else …`, and the checks whose failures select its trap exits. A dispatch block shows the layouts that select each call. Names come from the `NAME` section and source locations from the source map, as comments marked `provenance`.

This is `sum_to` from `tests/backend/programs/computation.nra`:

```text
function f1(s0: int32) -> int32  // provenance: Computation.sum_to
  frame: s0:int32(limit) s1:int32(total) s2:int32(current)
  b0: [decoded]  // provenance: computation.nra:7:9
    { s1 := 0, s2 := 1 } goto b1
  b1: [lifted: structural] [reconstructed: loop header; back edges from b2]  // provenance: computation.nra:9:9
    if (s2 <= s0) then goto b2
    else goto b3
  b2: [lifted: structural]  // provenance: computation.nra:10:21
    check (s1 + s2) -> trap 1 (arithmetic failure)
    check (s2 + 1) -> trap 1 (arithmetic failure)
    exit 0: { s1 := (s1 + s2), s2 := (s2 + 1) } goto b1
    exit 1: trap 1 (arithmetic failure)
  b3: [decoded]  // provenance: computation.nra:13:9
    return s1
```

The reconstruction form is a description of the image, not Neura source: slots, blocks, and parallel assignments appear as the image has them, and the text is not meant to be compiled.

## Tasks and scheduler operations

An image with scheduler exits or polls (NVR 1.5) decompiles like any other. A scheduler operation or a poll is an exit field, not something the graph computes, so its evidence class is the first one, `[decoded]`: it is read from the exit record and from nothing else.

| Item | How it is shown | Evidence |
| --- | --- | --- |
| A scheduler exit | `s5 := spawn f4(s4) [decoded]; goto b6`, `s2 := join(s1) [decoded]`, `scope-enter [decoded]`, and the other operations by their assembly names | Decoded from the exit record |
| A poll | `poll [decoded];` before the continue or call it marks | Decoded from the tag bit |
| A scope pair | `[reconstructed: scope b0.e0]` on a `scope-enter` and its `scope-exit` | Structural inference from control flow: the pair is shown only when every path to a scope operation or return agrees on the stack of open scopes. Otherwise each operation says `[scope pair unresolved]` |
| A block with a polling call or continue | Lifted as any other block | The poll does not change what the graph computes or which exit it selects |
| A block with a scheduler exit | `[decoded]` when its graph computes nothing; otherwise `[circuit]` | The effect of the exit is outside the graph model, so the block's graph is shown as lanes and not lifted |
| The `TRNS` table | `transferable-layouts [ ... ]` in the lossless form | Decoded; the loader has validated it |

Names, spawn sites, and source locations come from optional metadata only. None of this needs the `SMAP` or `NAME` sections, and `--no-provenance` changes only the comments. The decompiler does not lift scheduling behavior, so it makes no claim about which task runs when. Regenerated images (`resynthesize`) keep scheduler exits and polls unchanged. `nvm::record` and `nvm::replay` accept images with tasks (trace file version 3), so a regenerated task image can be compared with its original by replaying the original's trace, which is an observed match under that trace. The `decompiler-behavior` corpus holds no task image yet; `concurrency-programs` checks that every task fixture reassembles to the same bytes and decompiles with its scheduler operations labeled and its scopes paired.

## Lifting

Lifting works on one block at a time and uses only the block's graph, inputs, and exits:

1. **Targets.** The values the exits read from the graph, each a typed lane group, and, for a block with two normal exits, the condition that selects between them.
2. **Observation.** The graph is evaluated on 40 pseudo-random and boundary input samples, plus samples at the constants that equality neurons spell (a ReLU reading every lane of an input word with weights of +1 and -1 tests equality with the constant those signs spell).
3. **Candidates.** For each target, typed operations over the input words that the target's lanes depend on, as the graph's wiring shows: the operations of [Numbers](lang/Numbers.md) and Boolean logic, one or two levels deep, with conversions and with constants. Constants come from small values, from spelled equality constants, and from probing the graph: an addend is the output at zero, a factor the output at one, and a comparison bound is found by bisection along the type's order (for floats, the order of non-NaN values). Candidates are filtered by evaluating them on the samples with word-level arithmetic; the filter is only a filter.
4. **Proof.** Each surviving combination is replayed through `BlockSynthesizer`, the converter's own block assembler: the block's inputs in their recorded order, the candidate operations in evaluation order with their checks, and the block's exits with the candidate values. The candidate is accepted only if the replayed block and graph are identical to the image's. Because the comparison is exact equality of the executable records, the accepted operations compute exactly what the image computes, on every input.
5. **Fallback.** A block with no structural match and at most 20 input lanes is accepted when a candidate agrees with the graph on all 2^n inputs. Otherwise the block stays a circuit.

Dispatch blocks are recognized separately: the graph is evaluated for every layout's tag to find the exit each selects, and the block is regenerated the way the converter builds dispatch blocks and compared.

Lifting recognizes the circuits the compiler emits in both optimized and unoptimized images, because both are built by the same circuit library. A graph whose weights were edited no longer matches its original operations: an edited block is either lifted as whatever it now computes, still proven by identical regeneration or exhaustive evaluation, or shown as a circuit. Stale metadata cannot affect the result, because metadata is never read during lifting.

Lifting leaves a block in circuit form when the search cannot recover its expression, such as a deeply nested operation, an undiscovered constant, or a check whose value no exit reads. Bitwise and shift graphs can also remain in circuit form. In the repository's test corpus, 1,536 of 1,608 blocks that compute something lift structurally, and the remaining 72 are shown as circuits.

## Re-synthesis and behavior comparison

`decompiler::resynthesize` builds a new image in which every structurally lifted block's graph is replaced by the graph regenerated from its lifted operations, appended as a new graph record, while unlifted blocks keep their original graphs. `nvm::record` runs the original image with real host services and records each host request and response and the terminal outcome; `nvm::replay` runs the regenerated image against that trace. The two must make the same requests, by import name, version, and arguments, in the same order, and end the same way: the same status, or the same trap kind and reason.

## Evidence

| Test | What it establishes | Kind of check |
| --- | --- | --- |
| `decompiler-lossless` | Every corpus image (every test and example program, optimized and unoptimized, with and without the source map, plus hand-assembled and edited images) disassembles to text that assembles to the same model and the same bytes, and disassembly is stable. Assembly errors name their line. | Exact, over the corpus |
| `decompiler-lift` | Every structural claim in the corpus is re-checked by regeneration. At least 90% of graph blocks lift structurally. Lifted text names the source operations for known programs. A block whose weight is edited is not lifted as its original operation, and every other block keeps its evidence. Output without provenance equals output from an image with the source map and names removed. | Structural, over the corpus |
| `decompiler-behavior` | The regenerated image of every compiled corpus image replays the original's host trace and outcome, with and without program arguments, including programs with conditional output and programs that trap. Scalar functions of two fixtures return the same result or trap on every 8-bit input in the original and the regenerated image. | Sampled inputs for programs; exhaustive over 8-bit domains for the fixture functions |

The behavior comparison follows from the structural proof for lifted blocks; it checks the regeneration, trace, and replay machinery end to end. Programs are compared on the inputs they are run with, not on all inputs.

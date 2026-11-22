# Resume-frame layout in 0.3.1

## Cause and alternatives

Flattened aggregate results occupy their full word count in the executable
frame. In the Mandelbrot workload, an eight-word `Result` increased the resume
frame from 34 to 42 slots. The measured inner iteration grew from 71 to 77
machine instructions, and the cold 6000 workload was 13.9% slower than rc.3.
These are measurements of that workload, not a general cost of value results.

Three possible remedies belong at different boundaries:

- Narrower flattening would change converter decisions and the emitted frame
  layout. It needs a separate representation decision and image review.
- Moving cold words out of the frame would change the execution interface,
  collector roots, and resume materialization. Every tier and supported image
  reader would need to agree on that storage.
- Lazy materialization of entry values can keep the existing frame and image.
  A word needed only to recover state can remain in its original slot while
  native code computes with the registers it actually reads.

## Decision

Keep the executable frame layout. In Tier 3 machine lowering, shorten the
lifetimes of entry loads whose original slots remain stable. Defer a load
when its recovery uses occur in more blocks inside loops than use blocks
outside loops. Each use block materializes the value once. Ordinary data
operands of write-backs inside loops keep their entry loads. The allocator
can leave a deferred value in its existing frame home, and a load assigned
to its source slot emits no machine instruction.

The proof must exclude ordinary data uses and phi inputs, writes of a different
value into the slot, call results that overlap it, and other values assigned
the same spill home. Keep the homes established before shortening lifetimes;
recomputing them could reuse a deferred source slot. Recovery still names the
original Tier 3 value, including all levels of an inlined state. Unknown or
changing slots retain the existing lowering. This is a native storage choice;
it changes neither initialization flags nor the language's alias rules.

## Images and goldens

No converter, serializer, NVR section, version, or frame-slot numbering changes.
Existing image and golden bytes must remain identical through emission. Native
machine code is expected to change and is checked separately, together with
output, step limits, deoptimization, and reference execution. Performance
acceptance requires the same cold harness and fresh instruction counts; a
source-level explanation alone does not establish the regression is fixed.

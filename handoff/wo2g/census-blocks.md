### binary-trees (size 16)

Steps 209,899,833 (block steps; kernel-charged steps are not blocks); in regions 67,647,489 (32.2%); outside 142,252,344 (67.8%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 44,958,282 | 31.6% |
| call (direct) | 44,957,945 | 31.6% |
| object allocation | 14,986,017 | 10.5% |
| continue: no exit stays in the region (successor: return) | 7,536,640 | 5.3% |
| continue: no exit stays in the region (successor: call) | 7,450,264 | 5.2% |
| inline WriteField: no exit stays in the region (successor: call) | 7,449,262 | 5.2% |
| inline WriteField: no exit stays in the region (successor: return) | 7,449,262 | 5.2% |
| inline ReadField: no exit stays in the region (successor: call) | 7,449,258 | 5.2% |
| (other categories) | 15,414 | 0.0% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f4 BinaryTrees.bottom_up | 0 | binary-trees.nra:20:20 | 14,985,902 | 10.5% | object allocation |
| f8 BinaryTrees.TreeNode.init | 2 | binary-trees.nra:16:9 | 14,985,902 | 21.1% | return |
| f4 BinaryTrees.bottom_up | 4 | binary-trees.nra:25:9 | 14,985,902 | 31.6% | return |
| f4 BinaryTrees.bottom_up | 1 | binary-trees.nra:20:20 | 14,985,902 | 42.1% | call (direct) → BinaryTrees.TreeNode.init |
| f4 BinaryTrees.bottom_up | 2 | binary-trees.nra:21:9 | 7,536,640 | 47.4% | continue: no exit stays in the region (successor: return) |
| f5 BinaryTrees.check | 2 | binary-trees.nra:30:13 | 7,536,640 | 52.7% | return |
| f4 BinaryTrees.bottom_up | 3 | binary-trees.nra:22:35 | 7,449,262 | 58.0% | call (direct) → BinaryTrees.bottom_up |
| f5 BinaryTrees.check | 4 | binary-trees.nra:32:20 | 7,449,262 | 63.2% | call (direct) → BinaryTrees.check |
| f5 BinaryTrees.check | 6 | binary-trees.nra:32:39 | 7,449,262 | 68.4% | call (direct) → BinaryTrees.check |
| f4 BinaryTrees.bottom_up | 5 | binary-trees.nra:22:13 | 7,449,262 | 73.7% | inline WriteField: no exit stays in the region (successor: call) |
| f5 BinaryTrees.check | 7 | binary-trees.nra:32:16 | 7,449,262 | 78.9% | return |
| f4 BinaryTrees.bottom_up | 6 | binary-trees.nra:23:36 | 7,449,262 | 84.2% | call (direct) → BinaryTrees.bottom_up |
| f4 BinaryTrees.bottom_up | 7 | binary-trees.nra:23:13 | 7,449,262 | 89.4% | inline WriteField: no exit stays in the region (successor: return) |
| f4 BinaryTrees.bottom_up | 2 | binary-trees.nra:21:9 | 7,449,258 | 94.6% | continue: no exit stays in the region (successor: call) |
| f5 BinaryTrees.check | 5 | binary-trees.nra:32:16 | 7,449,258 | 99.9% | inline ReadField: no exit stays in the region (successor: call) |

### closures (size 400)

Steps 12,268,521 (block steps; kernel-charged steps are not blocks); in regions 8,267,878 (67.4%); outside 4,000,643 (32.6%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 1,202,040 | 30.0% |
| call (closure, through a dispatch block) | 1,197,200 | 29.9% |
| inline ReadField: no exit stays in the region (successor: return) | 799,996 | 20.0% |
| state op AppendElement | 758,218 | 19.0% |
| continue: region not formed yet (counting) | 10,637 | 0.3% |
| continue: region not formed yet (formed later) | 5,994 | 0.1% |
| call (direct) | 4,838 | 0.1% |
| inline ReadTag: region not formed yet (counting) | 4,006 | 0.1% |
| (other categories) | 17,714 | 0.4% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f35 $closure785.$invoke | 1 | closures.nra:61:78 | 400,000 | 10.0% | return |
| f36 $closure792.$invoke | 1 | closures.nra:62:78 | 400,000 | 20.0% | return |
| f7 Closures.filter_values | 8 | closures.nra:29:17 | 400,000 | 30.0% | call (closure, through a dispatch block) → $closure792.$invoke |
| f5 Closures.map_values | 8 | closures.nra:21:27 | 400,000 | 40.0% | call (closure, through a dispatch block) → $closure785.$invoke |
| f35 $closure785.$invoke | 0 | closures.nra:61:86 | 400,000 | 50.0% | inline ReadField: no exit stays in the region (successor: return) |
| f5 Closures.map_values | 9 | closures.nra:21:13 | 400,000 | 60.0% | state op AppendElement |
| f36 $closure792.$invoke | 0 | closures.nra:62:92 | 399,996 | 70.0% | inline ReadField: no exit stays in the region (successor: return) |
| f37 $closure799.$invoke | 3 | closures.nra:63:90 | 357,200 | 78.9% | return |
| f9 Closures.fold_values | 7 | closures.nra:39:21 | 357,200 | 87.8% | call (closure, through a dispatch block) → $closure799.$invoke |
| f7 Closures.filter_values | 11 | closures.nra:30:17 | 357,200 | 96.8% | state op AppendElement |

### collatz (size 100000)

Steps 43,516,354 (block steps; kernel-charged steps are not blocks); in regions 43,511,327 (100.0%); outside 5,027 (0.0%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| continue: region not formed yet (counting) | 3,157 | 62.8% |
| continue: region not formed yet (formed later) | 999 | 19.9% |
| inline ReadField: region not formed yet (counting) | 265 | 5.3% |
| block not lifted (below threshold) | 195 | 3.9% |
| return | 72 | 1.4% |
| call (direct) | 70 | 1.4% |
| inline WriteField: region not formed yet (counting) | 59 | 1.2% |
| inline ReadLength: region not formed yet (counting) | 45 | 0.9% |
| inline ReadElement: region not formed yet (counting) | 43 | 0.9% |
| inline ReadTag: region not formed yet (counting) | 40 | 0.8% |
| state op AppendElement | 26 | 0.5% |
| object allocation | 15 | 0.3% |
| inline TextEqual: region not formed yet (counting) | 15 | 0.3% |
| inline WriteGlobal: region not formed yet (counting) | 7 | 0.1% |
| (other categories) | 19 | 0.4% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f1 Collatz.main | 8 | collatz.nra:19:13 | 999 | 19.9% | continue: region not formed yet (formed later) |
| f1 Collatz.main | 12 | collatz.nra:25:26 | 953 | 38.8% | continue: region not formed yet (counting) |
| f1 Collatz.main | 9 | collatz.nra:20:17 | 953 | 57.8% | continue: region not formed yet (counting) |
| f1 Collatz.main | 11 | collatz.nra:21:21 | 658 | 70.9% | continue: region not formed yet (counting) |
| f1 Collatz.main | 13 | collatz.nra:23:29 | 291 | 76.7% | continue: region not formed yet (counting) |
| f1 Collatz.main | 5 | collatz.nra:17:13 | 47 | 77.6% | continue: region not formed yet (counting) |
| f1 Collatz.main | 4 | collatz.nra:16:9 | 43 | 78.5% | continue: region not formed yet (counting) |
| f1 Collatz.main | 10 | collatz.nra:27:21 | 42 | 79.3% | continue: region not formed yet (counting) |
| f1 Collatz.main | 7 | collatz.nra:16:49 | 42 | 80.1% | continue: region not formed yet (counting) |
| f32 StringBuilder.StringBuilder.append | 2 | std/core/string-builder.nra:34:13 | 29 | 80.7% | continue: region not formed yet (counting) |
| f32 StringBuilder.StringBuilder.append | 3 | std/core/string-builder.nra:35:17 | 26 | 81.2% | inline ReadField: region not formed yet (counting) |
| f32 StringBuilder.StringBuilder.append | 7 | std/core/string-builder.nra:35:17 | 26 | 81.7% | state op AppendElement |
| f32 StringBuilder.StringBuilder.append | 5 | std/core/string-builder.nra:34:40 | 22 | 82.2% | continue: region not formed yet (counting) |
| f32 StringBuilder.StringBuilder.append | 6 | std/core/string-builder.nra:35:30 | 22 | 82.6% | inline ReadElement: region not formed yet (counting) |
| f38 Formatting.Cursor.peek | 8 | std/io/internal/formatting.nra:269:13 | 15 | 82.9% | return |
| f38 Formatting.Cursor.peek | 6 | std/io/internal/formatting.nra:269:36 | 15 | 83.2% | inline ReadField: region not formed yet (counting) |
| f38 Formatting.Cursor.peek | 5 | std/io/internal/formatting.nra:269:20 | 15 | 83.5% | inline ReadField: region not formed yet (counting) |
| f38 Formatting.Cursor.peek | 1 | std/io/internal/formatting.nra:266:29 | 15 | 83.8% | inline ReadField: region not formed yet (counting) |
| f38 Formatting.Cursor.peek | 2 | std/io/internal/formatting.nra:266:29 | 15 | 84.1% | inline ReadLength: region not formed yet (counting) |
| f38 Formatting.Cursor.peek | 0 | std/io/internal/formatting.nra:266:17 | 15 | 84.4% | inline ReadField: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 4 | std/io/internal/formatting.nra:63:31 | 14 | 84.7% | inline ReadLength: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 3 | std/io/internal/formatting.nra:63:31 | 14 | 85.0% | inline ReadField: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 2 | std/io/internal/formatting.nra:63:20 | 14 | 85.2% | inline ReadField: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 8 | std/io/internal/formatting.nra:64:42 | 13 | 85.5% | inline ReadField: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 6 | std/io/internal/formatting.nra:64:26 | 13 | 85.8% | inline ReadField: region not formed yet (counting) |
| f38 Formatting.Cursor.peek | 7 | std/io/internal/formatting.nra:269:20 | 11 | 86.0% | inline ReadElement: region not formed yet (counting) |
| f38 Formatting.Cursor.peek | 3 | std/io/internal/formatting.nra:266:29 | 11 | 86.2% | continue: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 11 | std/io/internal/formatting.nra:66:21 | 10 | 86.4% | inline ReadField: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 5 | std/io/internal/formatting.nra:63:31 | 10 | 86.6% | continue: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 10 | std/io/internal/formatting.nra:65:17 | 9 | 86.8% | continue: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 9 | std/io/internal/formatting.nra:64:26 | 9 | 87.0% | inline ReadElement: region not formed yet (counting) |
| f1 Collatz.main | 14 | collatz.nra:29:17 | 9 | 87.1% | continue: region not formed yet (counting) |
| f32 StringBuilder.StringBuilder.append | 4 | std/core/string-builder.nra:37:20 | 7 | 87.3% | inline ReadField: region not formed yet (counting) |
| f32 StringBuilder.StringBuilder.append | 8 | std/core/string-builder.nra:37:20 | 7 | 87.4% | inline ReadField: region not formed yet (counting) |
| f32 StringBuilder.StringBuilder.append | 9 | std/core/string-builder.nra:37:13 | 7 | 87.5% | return |
| f32 StringBuilder.StringBuilder.append | 0 | std/core/string-builder.nra:33:26 | 7 | 87.7% | inline ReadLength: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 13 | std/io/internal/formatting.nra:66:21 | 6 | 87.8% | inline WriteField: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 5 | std/io/internal/formatting.nra:63:31 | 4 | 87.9% | block not lifted (below threshold) |
| f24 Formatting.Cursor.render | 13 | std/io/internal/formatting.nra:66:21 | 4 | 88.0% | block not lifted (below threshold) |
| f1 Collatz.main | 11 | collatz.nra:21:21 | 4 | 88.0% | block not lifted (below threshold) |
| f1 Collatz.main | 13 | collatz.nra:23:29 | 4 | 88.1% | block not lifted (below threshold) |
| f1 Collatz.main | 12 | collatz.nra:25:26 | 4 | 88.2% | block not lifted (below threshold) |
| f1 Collatz.main | 4 | collatz.nra:16:9 | 4 | 88.3% | block not lifted (below threshold) |
| f1 Collatz.main | 10 | collatz.nra:27:21 | 4 | 88.4% | block not lifted (below threshold) |
| f1 Collatz.main | 9 | collatz.nra:20:17 | 4 | 88.4% | block not lifted (below threshold) |
| f1 Collatz.main | 7 | collatz.nra:16:49 | 4 | 88.5% | block not lifted (below threshold) |
| f1 Collatz.main | 8 | collatz.nra:19:13 | 4 | 88.6% | block not lifted (below threshold) |
| f24 Formatting.Cursor.render | 9 | std/io/internal/formatting.nra:64:26 | 4 | 88.7% | block not lifted (below threshold) |
| f32 StringBuilder.StringBuilder.append | 1 | std/core/string-builder.nra:33:26 | 4 | 88.8% | block not lifted (below threshold) |
| f38 Formatting.Cursor.peek | 7 | std/io/internal/formatting.nra:269:20 | 4 | 88.8% | block not lifted (below threshold) |
| f32 StringBuilder.StringBuilder.append | 2 | std/core/string-builder.nra:34:13 | 4 | 88.9% | block not lifted (below threshold) |
| f32 StringBuilder.StringBuilder.append | 5 | std/core/string-builder.nra:34:40 | 4 | 89.0% | block not lifted (below threshold) |
| f32 StringBuilder.StringBuilder.append | 6 | std/core/string-builder.nra:35:30 | 4 | 89.1% | block not lifted (below threshold) |
| f38 Formatting.Cursor.peek | 3 | std/io/internal/formatting.nra:266:29 | 4 | 89.2% | block not lifted (below threshold) |
| f24 Formatting.Cursor.render | 10 | std/io/internal/formatting.nra:65:17 | 4 | 89.2% | block not lifted (below threshold) |
| f37 Formatting.Cursor.read_index | 44 | std/io/internal/formatting.nra:143:21 | 3 | 89.3% | block not lifted (below threshold) |
| f24 Formatting.Cursor.render | 68 | std/io/internal/formatting.nra:88:33 | 3 | 89.4% | continue: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 30 | std/io/internal/formatting.nra:88:33 | 3 | 89.4% | inline ReadField: region not formed yet (counting) |
| f32 StringBuilder.StringBuilder.append | 1 | std/core/string-builder.nra:33:26 | 3 | 89.5% | continue: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 58 | std/io/internal/formatting.nra:81:21 | 3 | 89.5% | call (direct) → StringBuilder.StringBuilder.append |
| f24 Formatting.Cursor.render | 46 | std/io/internal/formatting.nra:81:21 | 3 | 89.6% | inline ReadTag: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 42 | std/io/internal/formatting.nra:76:25 | 3 | 89.7% | inline ReadField: region not formed yet (counting) |
| f33 Formatting.Cursor.replacement | 43 | std/io/internal/formatting.nra:121:13 | 3 | 89.7% | block not lifted (below threshold) |
| f33 Formatting.Cursor.replacement | 42 | std/io/internal/formatting.nra:121:13 | 3 | 89.8% | inline ReadField: region not formed yet (counting) |
| f47 Formatting.Cursor.render_signed | 4 | std/io/internal/formatting.nra:332:17 | 3 | 89.8% | return |
| f24 Formatting.Cursor.render | 44 | std/io/internal/formatting.nra:76:25 | 3 | 89.9% | continue: region not formed yet (counting) |
| f56 Formatting.pad | 7 | std/io/internal/formatting.nra:539:13 | 3 | 90.0% | return |
| f56 Formatting.pad | 5 | std/io/internal/formatting.nra:539:35 | 3 | 90.0% | continue: region not formed yet (counting) |
| f56 Formatting.pad | 2 | std/io/internal/formatting.nra:539:20 | 3 | 90.1% | inline TextEqual: region not formed yet (counting) |
| f56 Formatting.pad | 1 | std/io/internal/formatting.nra:538:13 | 3 | 90.1% | block not lifted (below threshold) |
| f56 Formatting.pad | 0 | std/io/internal/formatting.nra:538:13 | 3 | 90.2% | inline ReadField: region not formed yet (counting) |
| f48 Formatting.Cursor.render_integer | 49 | std/io/internal/formatting.nra:378:17 | 3 | 90.3% | inline ReadField: region not formed yet (counting) |
| f40 Formatting.Cursor.render_value | 10 | std/io/internal/formatting.nra:285:13 | 3 | 90.3% | continue: region not formed yet (counting) |
| f48 Formatting.Cursor.render_integer | 54 | std/io/internal/formatting.nra:381:13 | 3 | 90.4% | return |
| f37 Formatting.Cursor.read_index | 26 | std/io/internal/formatting.nra:138:24 | 3 | 90.4% | call (direct) → Formatting.Cursor.peek |
| f33 Formatting.Cursor.replacement | 19 | std/io/internal/formatting.nra:109:17 | 3 | 90.5% | inline ReadField: region not formed yet (counting) |
| f33 Formatting.Cursor.replacement | 15 | std/io/internal/formatting.nra:105:17 | 3 | 90.6% | inline ReadTag: region not formed yet (counting) |
| f33 Formatting.Cursor.replacement | 13 | std/io/internal/formatting.nra:104:31 | 3 | 90.6% | object allocation |
| f33 Formatting.Cursor.replacement | 16 | std/io/internal/formatting.nra:105:17 | 3 | 90.7% | call (direct) → Formatting.Cursor.peek |
| f48 Formatting.Cursor.render_integer | 31 | std/io/internal/formatting.nra:355:17 | 3 | 90.7% | block not lifted (below threshold) |
| f33 Formatting.Cursor.replacement | 10 | std/io/internal/formatting.nra:101:17 | 3 | 90.8% | inline TextEqual: region not formed yet (counting) |
| f35 Formatting.Spec.init | 8 | std/io/internal/formatting.nra:30:9 | 3 | 90.8% | return |
| f35 Formatting.Spec.init | 6 | std/io/internal/formatting.nra:28:9 | 3 | 90.9% | inline WriteField: region not formed yet (counting) |
| f35 Formatting.Spec.init | 5 | std/io/internal/formatting.nra:26:9 | 3 | 91.0% | inline WriteField: region not formed yet (counting) |
| f33 Formatting.Cursor.replacement | 2 | std/io/internal/formatting.nra:96:29 | 3 | 91.0% | inline ReadLength: region not formed yet (counting) |
| f35 Formatting.Spec.init | 4 | std/io/internal/formatting.nra:24:9 | 3 | 91.1% | inline WriteField: region not formed yet (counting) |
| f33 Formatting.Cursor.replacement | 5 | std/io/internal/formatting.nra:100:26 | 3 | 91.1% | inline ReadTag: region not formed yet (counting) |
| f35 Formatting.Spec.init | 7 | std/io/internal/formatting.nra:30:9 | 3 | 91.2% | inline WriteField: region not formed yet (counting) |
| f33 Formatting.Cursor.replacement | 9 | std/io/internal/formatting.nra:101:17 | 3 | 91.3% | inline ReadField: region not formed yet (counting) |
| f33 Formatting.Cursor.replacement | 11 | std/io/internal/formatting.nra:101:17 | 3 | 91.3% | continue: region not formed yet (counting) |
| f37 Formatting.Cursor.read_index | 28 | std/io/internal/formatting.nra:138:40 | 3 | 91.4% | inline ReadTag: region not formed yet (counting) |
| f37 Formatting.Cursor.read_index | 31 | std/io/internal/formatting.nra:138:40 | 3 | 91.4% | block not lifted (below threshold) |
| f33 Formatting.Cursor.replacement | 27 | std/io/internal/formatting.nra:112:17 | 3 | 91.5% | call (direct) → Formatting.Cursor.peek |
| f33 Formatting.Cursor.replacement | 28 | std/io/internal/formatting.nra:112:17 | 3 | 91.6% | block not lifted (below threshold) |
| f48 Formatting.Cursor.render_integer | 43 | std/io/internal/formatting.nra:373:13 | 3 | 91.6% | block not lifted (below threshold) |
| f40 Formatting.Cursor.render_value | 12 | std/io/internal/formatting.nra:285:35 | 3 | 91.7% | call (direct) → Formatting.Cursor.render_signed |
| f33 Formatting.Cursor.replacement | 14 | std/io/internal/formatting.nra:104:31 | 3 | 91.7% | call (direct) → Formatting.Spec.init |
| f48 Formatting.Cursor.render_integer | 33 | std/io/internal/formatting.nra:358:13 | 3 | 91.8% | block not lifted (below threshold) |
| f56 Formatting.pad | 4 | std/io/internal/formatting.nra:539:20 | 3 | 91.9% | block not lifted (below threshold) |
| f24 Formatting.Cursor.render | 12 | std/io/internal/formatting.nra:69:31 | 3 | 91.9% | inline ReadField: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 15 | std/io/internal/formatting.nra:69:31 | 3 | 92.0% | text building (TextSlice) |
| f37 Formatting.Cursor.read_index | 29 | std/io/internal/formatting.nra:138:24 | 3 | 92.0% | block not lifted (below threshold) |
| f48 Formatting.Cursor.render_integer | 40 | std/io/internal/formatting.nra:368:20 | 3 | 92.1% | block not lifted (below threshold) |
| f24 Formatting.Cursor.render | 22 | std/io/internal/formatting.nra:71:32 | 3 | 92.2% | inline ReadLength: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 21 | std/io/internal/formatting.nra:71:32 | 3 | 92.2% | inline ReadField: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 25 | std/io/internal/formatting.nra:71:21 | 3 | 92.3% | block not lifted (below threshold) |
| f33 Formatting.Cursor.replacement | 30 | std/io/internal/formatting.nra:120:38 | 3 | 92.3% | inline ReadField: region not formed yet (counting) |
| f48 Formatting.Cursor.render_integer | 45 | std/io/internal/formatting.nra:377:48 | 3 | 92.4% | inline ReadField: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 20 | std/io/internal/formatting.nra:71:21 | 3 | 92.5% | inline ReadField: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 16 | std/io/internal/formatting.nra:69:17 | 3 | 92.5% | inline ReadTag: region not formed yet (counting) |
| f37 Formatting.Cursor.read_index | 58 | std/io/internal/formatting.nra:152:13 | 3 | 92.6% | return |
| f24 Formatting.Cursor.render | 19 | std/io/internal/formatting.nra:70:17 | 3 | 92.6% | block not lifted (below threshold) |
| f37 Formatting.Cursor.read_index | 33 | std/io/internal/formatting.nra:138:24 | 3 | 92.7% | block not lifted (below threshold) |
| f24 Formatting.Cursor.render | 41 | std/io/internal/formatting.nra:75:34 | 3 | 92.8% | call (direct) → Formatting.Cursor.replacement |
| f37 Formatting.Cursor.read_index | 51 | std/io/internal/formatting.nra:145:37 | 3 | 92.8% | inline ReadLength: region not formed yet (counting) |
| f37 Formatting.Cursor.read_index | 54 | std/io/internal/formatting.nra:147:24 | 3 | 92.9% | block not lifted (below threshold) |
| f37 Formatting.Cursor.read_index | 6 | std/io/internal/formatting.nra:142:17 | 3 | 92.9% | inline ReadField: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 23 | std/io/internal/formatting.nra:71:32 | 3 | 93.0% | block not lifted (below threshold) |
| f33 Formatting.Cursor.replacement | 40 | std/io/internal/formatting.nra:120:25 | 3 | 93.1% | inline ReadTag: region not formed yet (counting) |
| f48 Formatting.Cursor.render_integer | 38 | std/io/internal/formatting.nra:363:20 | 3 | 93.1% | block not lifted (below threshold) |
| f33 Formatting.Cursor.replacement | 24 | std/io/internal/formatting.nra:109:17 | 3 | 93.2% | continue: region not formed yet (counting) |
| f48 Formatting.Cursor.render_integer | 37 | std/io/internal/formatting.nra:372:24 | 3 | 93.2% | call (direct) → UInt64.to_string_radix |
| f24 Formatting.Cursor.render | 26 | std/io/internal/formatting.nra:71:71 | 3 | 93.3% | inline ReadField: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 24 | std/io/internal/formatting.nra:71:55 | 3 | 93.4% | inline ReadField: region not formed yet (counting) |
| f33 Formatting.Cursor.replacement | 44 | std/io/internal/formatting.nra:122:13 | 3 | 93.4% | return |
| f24 Formatting.Cursor.render | 27 | std/io/internal/formatting.nra:71:55 | 3 | 93.5% | block not lifted (below threshold) |
| f40 Formatting.Cursor.render_value | 4 | std/io/internal/formatting.nra:283:75 | 3 | 93.5% | return |
| f48 Formatting.Cursor.render_integer | 48 | std/io/internal/formatting.nra:377:26 | 3 | 93.6% | call (direct) → Formatting.number_sign |
| f24 Formatting.Cursor.render | 28 | std/io/internal/formatting.nra:71:55 | 3 | 93.7% | block not lifted (below threshold) |
| f37 Formatting.Cursor.read_index | 41 | std/io/internal/formatting.nra:142:17 | 3 | 93.7% | block not lifted (below threshold) |
| f37 Formatting.Cursor.read_index | 42 | std/io/internal/formatting.nra:143:21 | 3 | 93.8% | inline ReadField: region not formed yet (counting) |
| f37 Formatting.Cursor.read_index | 46 | std/io/internal/formatting.nra:143:21 | 3 | 93.8% | block not lifted (below threshold) |
| f33 Formatting.Cursor.replacement | 41 | std/io/internal/formatting.nra:120:25 | 3 | 93.9% | call (direct) → Formatting.Cursor.render_value |
| f37 Formatting.Cursor.read_index | 40 | std/io/internal/formatting.nra:142:17 | 3 | 94.0% | inline TextEqual: region not formed yet (counting) |
| f33 Formatting.Cursor.replacement | 39 | std/io/internal/formatting.nra:120:38 | 3 | 94.0% | block not lifted (below threshold) |
| f37 Formatting.Cursor.read_index | 57 | std/io/internal/formatting.nra:148:21 | 3 | 94.1% | block not lifted (below threshold) |
| f33 Formatting.Cursor.replacement | 17 | std/io/internal/formatting.nra:105:17 | 3 | 94.1% | block not lifted (below threshold) |
| f33 Formatting.Cursor.replacement | 26 | std/io/internal/formatting.nra:112:17 | 3 | 94.2% | inline ReadTag: region not formed yet (counting) |
| f59 Formatting.number_sign | 6 | std/io/internal/formatting.nra:533:9 | 3 | 94.3% | return |
| f37 Formatting.Cursor.read_index | 43 | std/io/internal/formatting.nra:151:13 | 3 | 94.3% | inline WriteField: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 18 | std/io/internal/formatting.nra:70:17 | 3 | 94.4% | inline ReadField: region not formed yet (counting) |
| f59 Formatting.number_sign | 4 | std/io/internal/formatting.nra:530:9 | 3 | 94.4% | block not lifted (below threshold) |
| f24 Formatting.Cursor.render | 14 | std/io/internal/formatting.nra:69:60 | 3 | 94.5% | inline ReadField: region not formed yet (counting) |
| f37 Formatting.Cursor.read_index | 56 | std/io/internal/formatting.nra:148:21 | 3 | 94.5% | inline ReadField: region not formed yet (counting) |
| f24 Formatting.Cursor.render | 17 | std/io/internal/formatting.nra:69:17 | 3 | 94.6% | call (direct) → StringBuilder.StringBuilder.append |
| f33 Formatting.Cursor.replacement | 8 | std/io/internal/formatting.nra:100:26 | 3 | 94.7% | call (direct) → Formatting.Cursor.read_index |
| f37 Formatting.Cursor.read_index | 30 | std/io/internal/formatting.nra:138:40 | 3 | 94.7% | call (direct) → Formatting.Cursor.peek |
| f33 Formatting.Cursor.replacement | 23 | std/io/internal/formatting.nra:109:17 | 3 | 94.8% | inline TextEqual: region not formed yet (counting) |
| f33 Formatting.Cursor.replacement | 1 | std/io/internal/formatting.nra:96:29 | 3 | 94.8% | inline ReadField: region not formed yet (counting) |
| f35 Formatting.Spec.init | 3 | std/io/internal/formatting.nra:22:9 | 3 | 94.9% | inline WriteField: region not formed yet (counting) |
| f48 Formatting.Cursor.render_integer | 52 | std/io/internal/formatting.nra:381:20 | 3 | 95.0% | call (direct) → Formatting.pad |
| f40 Formatting.Cursor.render_value | 8 | std/io/internal/formatting.nra:285:29 | 3 | 95.0% | inline ReadField: region not formed yet (counting) |

### dispatch (size 600)

Steps 9,854,095 (block steps; kernel-charged steps are not blocks); in regions 6,182,783 (62.7%); outside 3,671,312 (37.3%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 1,201,255 | 32.7% |
| call (through a dispatch block) | 1,200,000 | 32.7% |
| inline ReadField: no exit stays in the region (successor: return) | 599,996 | 16.3% |
| inline ReadTag: no exit stays in the region (successor: call) | 599,996 | 16.3% |
| continue: region not formed yet (counting) | 34,793 | 0.9% |
| inline ReadLength: region not formed yet (formed later) | 7,999 | 0.2% |
| inline ReadGlobal: region not formed yet (formed later) | 7,999 | 0.2% |
| inline ReadField: region not formed yet (counting) | 6,196 | 0.2% |
| (other categories) | 13,078 | 0.4% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f1 Dispatch.main | 37 | dispatch.nra:106:45 | 600,000 | 16.3% | call (through a dispatch block) → Dispatch.Circle.weight |
| f1 Dispatch.main | 39 | dispatch.nra:106:62 | 600,000 | 32.7% | call (through a dispatch block) → Dispatch.Rectangle.mix |
| f1 Dispatch.main | 38 | dispatch.nra:106:29 | 599,996 | 49.0% | inline ReadTag: no exit stays in the region (successor: call) |
| f33 Dispatch.Square.mix | 1 | dispatch.nra:30:57 | 249,000 | 55.8% | return |
| f33 Dispatch.Square.mix | 0 | dispatch.nra:30:65 | 249,000 | 62.6% | inline ReadField: no exit stays in the region (successor: return) |
| f28 Dispatch.Square.weight | 3 | dispatch.nra:28:48 | 129,600 | 66.1% | return |
| f30 Dispatch.Rectangle.weight | 3 | dispatch.nra:50:48 | 127,200 | 69.6% | return |
| f34 Dispatch.Rectangle.mix | 1 | dispatch.nra:52:57 | 127,200 | 73.1% | return |
| f34 Dispatch.Rectangle.mix | 0 | dispatch.nra:52:65 | 127,200 | 76.5% | inline ReadField: no exit stays in the region (successor: return) |
| f29 Dispatch.Cube.weight | 5 | dispatch.nra:38:57 | 119,400 | 79.8% | return |
| f36 Dispatch.Circle.mix | 1 | dispatch.nra:78:57 | 112,800 | 82.8% | return |
| f32 Dispatch.Circle.weight | 3 | dispatch.nra:76:48 | 112,800 | 85.9% | return |
| f36 Dispatch.Circle.mix | 0 | dispatch.nra:78:72 | 112,800 | 89.0% | inline ReadField: no exit stays in the region (successor: return) |
| f31 Dispatch.Triangle.weight | 3 | dispatch.nra:64:48 | 111,000 | 92.0% | return |
| f35 Dispatch.Triangle.mix | 1 | dispatch.nra:66:57 | 111,000 | 95.0% | return |

### fannkuch (size 9)

Steps 64,070,446 (block steps; kernel-charged steps are not blocks); in regions 64,059,129 (100.0%); outside 11,317 (0.0%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| continue: region not formed yet (counting) | 4,706 | 41.6% |
| inline ReadElement: region not formed yet (counting) | 2,623 | 23.2% |
| inline WriteElement: region not formed yet (counting) | 2,196 | 19.4% |
| continue: region not formed yet (formed later) | 999 | 8.8% |
| block not lifted (below threshold) | 276 | 2.4% |
| inline ReadField: region not formed yet (counting) | 198 | 1.7% |
| return | 59 | 0.5% |
| call (direct) | 57 | 0.5% |
| inline WriteField: region not formed yet (counting) | 48 | 0.4% |
| state op AppendElement | 43 | 0.4% |
| inline ReadLength: region not formed yet (counting) | 34 | 0.3% |
| inline ReadTag: region not formed yet (counting) | 28 | 0.2% |
| (other categories) | 50 | 0.4% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f1 Fannkuch.main | 15 | fannkuch.nra:30:13 | 999 | 8.8% | continue: region not formed yet (formed later) |
| f1 Fannkuch.main | 19 | fannkuch.nra:31:17 | 899 | 16.8% | inline WriteElement: region not formed yet (counting) |
| f1 Fannkuch.main | 18 | fannkuch.nra:30:40 | 899 | 24.7% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 16 | fannkuch.nra:31:27 | 899 | 32.7% | inline ReadElement: region not formed yet (counting) |
| f1 Fannkuch.main | 24 | fannkuch.nra:37:17 | 526 | 37.3% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 51 | fannkuch.nra:60:17 | 430 | 41.1% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 25 | fannkuch.nra:38:35 | 313 | 43.9% | inline ReadElement: region not formed yet (counting) |
| f1 Fannkuch.main | 28 | fannkuch.nra:39:36 | 313 | 46.6% | inline ReadElement: region not formed yet (counting) |
| f1 Fannkuch.main | 27 | fannkuch.nra:37:47 | 313 | 49.4% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 29 | fannkuch.nra:39:21 | 313 | 52.2% | inline WriteElement: region not formed yet (counting) |
| f1 Fannkuch.main | 30 | fannkuch.nra:40:26 | 313 | 54.9% | inline WriteElement: region not formed yet (counting) |
| f1 Fannkuch.main | 21 | fannkuch.nra:35:13 | 309 | 57.7% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 54 | fannkuch.nra:60:44 | 260 | 60.0% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 55 | fannkuch.nra:61:21 | 260 | 62.3% | inline WriteElement: region not formed yet (counting) |
| f1 Fannkuch.main | 52 | fannkuch.nra:61:38 | 260 | 64.6% | inline ReadElement: region not formed yet (counting) |
| f1 Fannkuch.main | 31 | fannkuch.nra:43:21 | 213 | 66.4% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 22 | fannkuch.nra:36:28 | 209 | 68.3% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 26 | fannkuch.nra:42:25 | 209 | 70.1% | inline ReadElement: region not formed yet (counting) |
| f1 Fannkuch.main | 11 | fannkuch.nra:26:13 | 175 | 71.7% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 50 | fannkuch.nra:60:17 | 170 | 73.2% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 39 | fannkuch.nra:59:29 | 170 | 74.7% | inline ReadElement: region not formed yet (counting) |
| f1 Fannkuch.main | 36 | fannkuch.nra:55:17 | 166 | 76.2% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 58 | fannkuch.nra:65:21 | 166 | 77.6% | inline ReadElement: region not formed yet (counting) |
| f1 Fannkuch.main | 59 | fannkuch.nra:65:21 | 166 | 79.1% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 57 | fannkuch.nra:64:28 | 166 | 80.6% | inline WriteElement: region not formed yet (counting) |
| f1 Fannkuch.main | 53 | fannkuch.nra:63:17 | 166 | 82.0% | inline WriteElement: region not formed yet (counting) |
| f1 Fannkuch.main | 56 | fannkuch.nra:64:28 | 166 | 83.5% | inline ReadElement: region not formed yet (counting) |
| f1 Fannkuch.main | 13 | fannkuch.nra:30:13 | 101 | 84.4% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 20 | fannkuch.nra:35:13 | 100 | 85.3% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 17 | fannkuch.nra:34:21 | 100 | 86.1% | inline ReadElement: region not formed yet (counting) |
| f1 Fannkuch.main | 23 | fannkuch.nra:45:13 | 96 | 87.0% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 33 | fannkuch.nra:48:13 | 96 | 87.8% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 37 | fannkuch.nra:70:27 | 96 | 88.7% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 14 | fannkuch.nra:28:21 | 74 | 89.3% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 12 | fannkuch.nra:27:23 | 74 | 90.0% | inline WriteElement: region not formed yet (counting) |
| f1 Fannkuch.main | 60 | fannkuch.nra:68:21 | 66 | 90.6% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 34 | fannkuch.nra:49:28 | 46 | 91.0% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 35 | fannkuch.nra:51:28 | 46 | 91.4% | continue: region not formed yet (counting) |
| f4 Lists.filled<int32> | 5 | std/collections/lists.nra:114:13 | 27 | 91.6% | state op AppendElement |
| f4 Lists.filled<int32> | 4 | std/collections/lists.nra:113:9 | 26 | 91.9% | continue: region not formed yet (counting) |
| f4 Lists.filled<int32> | 7 | std/collections/lists.nra:115:13 | 23 | 92.1% | continue: region not formed yet (counting) |
| f33 StringBuilder.StringBuilder.append | 2 | std/core/string-builder.nra:34:13 | 17 | 92.2% | continue: region not formed yet (counting) |
| f33 StringBuilder.StringBuilder.append | 3 | std/core/string-builder.nra:35:17 | 16 | 92.4% | inline ReadField: region not formed yet (counting) |
| f33 StringBuilder.StringBuilder.append | 7 | std/core/string-builder.nra:35:17 | 16 | 92.5% | state op AppendElement |
| f25 Formatting.Cursor.render | 4 | std/io/internal/formatting.nra:63:31 | 13 | 92.6% | inline ReadLength: region not formed yet (counting) |
| f25 Formatting.Cursor.render | 3 | std/io/internal/formatting.nra:63:31 | 13 | 92.7% | inline ReadField: region not formed yet (counting) |
| f25 Formatting.Cursor.render | 2 | std/io/internal/formatting.nra:63:20 | 13 | 92.8% | inline ReadField: region not formed yet (counting) |
| f25 Formatting.Cursor.render | 6 | std/io/internal/formatting.nra:64:26 | 12 | 92.9% | inline ReadField: region not formed yet (counting) |
| f25 Formatting.Cursor.render | 8 | std/io/internal/formatting.nra:64:42 | 12 | 93.1% | inline ReadField: region not formed yet (counting) |
| f33 StringBuilder.StringBuilder.append | 5 | std/core/string-builder.nra:34:40 | 12 | 93.2% | continue: region not formed yet (counting) |
| f33 StringBuilder.StringBuilder.append | 6 | std/core/string-builder.nra:35:30 | 12 | 93.3% | inline ReadElement: region not formed yet (counting) |
| f39 Formatting.Cursor.peek | 8 | std/io/internal/formatting.nra:269:13 | 10 | 93.4% | return |
| f25 Formatting.Cursor.render | 11 | std/io/internal/formatting.nra:66:21 | 10 | 93.4% | inline ReadField: region not formed yet (counting) |
| f39 Formatting.Cursor.peek | 6 | std/io/internal/formatting.nra:269:36 | 10 | 93.5% | inline ReadField: region not formed yet (counting) |
| f39 Formatting.Cursor.peek | 5 | std/io/internal/formatting.nra:269:20 | 10 | 93.6% | inline ReadField: region not formed yet (counting) |
| f39 Formatting.Cursor.peek | 1 | std/io/internal/formatting.nra:266:29 | 10 | 93.7% | inline ReadField: region not formed yet (counting) |
| f39 Formatting.Cursor.peek | 2 | std/io/internal/formatting.nra:266:29 | 10 | 93.8% | inline ReadLength: region not formed yet (counting) |
| f39 Formatting.Cursor.peek | 0 | std/io/internal/formatting.nra:266:17 | 10 | 93.9% | inline ReadField: region not formed yet (counting) |
| f25 Formatting.Cursor.render | 5 | std/io/internal/formatting.nra:63:31 | 9 | 94.0% | continue: region not formed yet (counting) |
| f25 Formatting.Cursor.render | 9 | std/io/internal/formatting.nra:64:26 | 8 | 94.0% | inline ReadElement: region not formed yet (counting) |
| f25 Formatting.Cursor.render | 10 | std/io/internal/formatting.nra:65:17 | 8 | 94.1% | continue: region not formed yet (counting) |
| f39 Formatting.Cursor.peek | 3 | std/io/internal/formatting.nra:266:29 | 6 | 94.2% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 32 | fannkuch.nra:46:17 | 6 | 94.2% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 7 | fannkuch.nra:18:9 | 6 | 94.3% | continue: region not formed yet (counting) |
| f39 Formatting.Cursor.peek | 7 | std/io/internal/formatting.nra:269:20 | 6 | 94.3% | inline ReadElement: region not formed yet (counting) |
| f25 Formatting.Cursor.render | 13 | std/io/internal/formatting.nra:66:21 | 6 | 94.4% | inline WriteField: region not formed yet (counting) |
| f1 Fannkuch.main | 10 | fannkuch.nra:18:36 | 5 | 94.4% | continue: region not formed yet (counting) |
| f1 Fannkuch.main | 8 | fannkuch.nra:19:13 | 5 | 94.5% | inline WriteElement: region not formed yet (counting) |
| f33 StringBuilder.StringBuilder.append | 8 | std/core/string-builder.nra:37:20 | 5 | 94.5% | inline ReadField: region not formed yet (counting) |
| f33 StringBuilder.StringBuilder.append | 4 | std/core/string-builder.nra:37:20 | 5 | 94.5% | inline ReadField: region not formed yet (counting) |
| f33 StringBuilder.StringBuilder.append | 9 | std/core/string-builder.nra:37:13 | 5 | 94.6% | return |
| f33 StringBuilder.StringBuilder.append | 0 | std/core/string-builder.nra:33:26 | 5 | 94.6% | inline ReadLength: region not formed yet (counting) |
| f1 Fannkuch.main | 55 | fannkuch.nra:61:21 | 4 | 94.7% | block not lifted (below threshold) |
| f1 Fannkuch.main | 24 | fannkuch.nra:37:17 | 4 | 94.7% | block not lifted (below threshold) |
| f1 Fannkuch.main | 51 | fannkuch.nra:60:17 | 4 | 94.7% | block not lifted (below threshold) |
| f1 Fannkuch.main | 52 | fannkuch.nra:61:38 | 4 | 94.8% | block not lifted (below threshold) |
| f1 Fannkuch.main | 35 | fannkuch.nra:51:28 | 4 | 94.8% | block not lifted (below threshold) |
| f1 Fannkuch.main | 23 | fannkuch.nra:45:13 | 4 | 94.8% | block not lifted (below threshold) |
| f1 Fannkuch.main | 34 | fannkuch.nra:49:28 | 4 | 94.9% | block not lifted (below threshold) |
| f1 Fannkuch.main | 26 | fannkuch.nra:42:25 | 4 | 94.9% | block not lifted (below threshold) |
| f1 Fannkuch.main | 30 | fannkuch.nra:40:26 | 4 | 95.0% | block not lifted (below threshold) |
| f1 Fannkuch.main | 25 | fannkuch.nra:38:35 | 4 | 95.0% | block not lifted (below threshold) |
| f1 Fannkuch.main | 37 | fannkuch.nra:70:27 | 4 | 95.0% | block not lifted (below threshold) |

### hash-map (size 200000)

Steps 52,186,850 (block steps; kernel-charged steps are not blocks); in regions 39,320,615 (75.3%); outside 12,866,235 (24.7%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 3,800,265 | 29.5% |
| state op AppendElement | 3,159,305 | 24.6% |
| call (through a dispatch block) | 2,350,081 | 18.3% |
| call (closure, through a dispatch block) | 900,000 | 7.0% |
| inline ReadTag: no exit stays in the region (successor: call) | 801,004 | 6.2% |
| inline ReadField: no exit stays in the region (successor: AppendElement) | 600,000 | 4.7% |
| call (direct) | 550,182 | 4.3% |
| object allocation | 150,020 | 1.2% |
| inline ReadField: region not formed yet (counting) | 140,429 | 1.1% |
| continue: region not formed yet (formed later) | 125,972 | 1.0% |
| continue: region not formed yet (counting) | 87,328 | 0.7% |
| inline ReadField: region not formed yet (formed later) | 45,982 | 0.4% |
| inline ReadLength: region not formed yet (counting) | 34,988 | 0.3% |
| inline ReadElement: region not formed yet (counting) | 32,775 | 0.3% |
| inline WriteField: region not formed yet (counting) | 28,309 | 0.2% |
| (other categories) | 59,595 | 0.5% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f32 HashCore.empty_index | 5 | std/collections/internal/hash-core.nra:299:13 | 1,048,568 | 8.1% | state op AppendElement |
| f43 HashMap.spread | 0 | std/collections/hash-map.nra:554:50 | 550,000 | 12.4% | return |
| f54 $closure1243<int64>.$invoke | 0 | std/collections/hash-map.nra:545:42 | 550,000 | 16.7% | return |
| f77 HashMap.reordered<int64> | 7 | std/collections/hash-map.nra:562:13 | 524,280 | 20.8% | state op AppendElement |
| f55 HashMap.HashMap<int64, int64>.find | 23 | std/collections/hash-map.nra:430:53 | 350,000 | 23.5% | call (closure, through a dispatch block) → $closure1247<int64>.$invoke |
| f62 $closure1247<int64>.$invoke | 0 | std/collections/hash-map.nra:550:44 | 350,000 | 26.2% | return |
| f55 HashMap.HashMap<int64, int64>.find | 25 | std/collections/hash-map.nra:431:21 | 350,000 | 28.9% | return |
| f52 HashMap.HashMapIterator<int64, int64>.has_next | 19 | std/collections/hash-map.nra:507:31 | 300,001 | 31.3% | return |
| f36 HashMap.HashMap<int64, int64>.set | 3 | std/collections/hash-map.nra:89:25 | 300,000 | 33.6% | call (direct) → HashMap.spread |
| f36 HashMap.HashMap<int64, int64>.set | 2 | std/collections/hash-map.nra:89:32 | 300,000 | 35.9% | call (closure, through a dispatch block) → $closure1243<int64>.$invoke |
| f36 HashMap.HashMap<int64, int64>.set | 5 | std/collections/hash-map.nra:90:25 | 300,000 | 38.3% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.find |
| f36 HashMap.HashMap<int64, int64>.set | 4 | std/collections/hash-map.nra:90:25 | 300,000 | 40.6% | inline ReadTag: no exit stays in the region (successor: call) |
| f96 HashCore.reordered_hashes | 7 | std/collections/internal/hash-core.nra:311:13 | 262,140 | 42.6% | state op AppendElement |
| f80 HashCore.HashCore.live_entries | 9 | std/collections/internal/hash-core.nra:175:21 | 262,140 | 44.7% | state op AppendElement |
| f97 HashCore.all_live | 3 | std/collections/internal/hash-core.nra:322:13 | 262,140 | 46.7% | state op AppendElement |
| f56 HashMap.HashMap<int64, int64>.insert | 20 | std/collections/hash-map.nra:447:13 | 200,000 | 48.3% | return |
| f1 HashMapBenchmark.main | 14 | hash-map.nra:24:29 | 200,000 | 49.8% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.get_or |
| f67 HashCore.HashCore.claim | 12 | std/collections/internal/hash-core.nra:133:13 | 200,000 | 51.4% | inline ReadField: no exit stays in the region (successor: AppendElement) |
| f56 HashMap.HashMap<int64, int64>.insert | 18 | std/collections/hash-map.nra:447:13 | 200,000 | 52.9% | inline ReadField: no exit stays in the region (successor: AppendElement) |
| f56 HashMap.HashMap<int64, int64>.insert | 17 | std/collections/hash-map.nra:446:13 | 200,000 | 54.5% | state op AppendElement |
| f56 HashMap.HashMap<int64, int64>.insert | 16 | std/collections/hash-map.nra:446:13 | 200,000 | 56.0% | inline ReadField: no exit stays in the region (successor: AppendElement) |
| f67 HashCore.HashCore.claim | 18 | std/collections/internal/hash-core.nra:136:13 | 200,000 | 57.6% | return |
| f67 HashCore.HashCore.claim | 11 | std/collections/internal/hash-core.nra:132:13 | 200,000 | 59.1% | state op AppendElement |
| f63 HashCore.HashCore.is_full | 5 | std/collections/internal/hash-core.nra:86:13 | 200,000 | 60.7% | return |
| f55 HashMap.HashMap<int64, int64>.find | 12 | std/collections/hash-map.nra:424:28 | 200,000 | 62.3% | return |
| f56 HashMap.HashMap<int64, int64>.insert | 19 | std/collections/hash-map.nra:447:13 | 200,000 | 63.8% | state op AppendElement |
| f1 HashMapBenchmark.main | 9 | hash-map.nra:20:13 | 200,000 | 65.4% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.set |
| f56 HashMap.HashMap<int64, int64>.insert | 15 | std/collections/hash-map.nra:445:13 | 200,000 | 66.9% | call (through a dispatch block) → HashCore.HashCore.claim |
| f36 HashMap.HashMap<int64, int64>.set | 14 | std/collections/hash-map.nra:95:13 | 200,000 | 68.5% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.insert |
| f36 HashMap.HashMap<int64, int64>.set | 15 | std/collections/hash-map.nra:95:13 | 200,000 | 70.0% | return |
| f67 HashCore.HashCore.claim | 13 | std/collections/internal/hash-core.nra:133:13 | 200,000 | 71.6% | state op AppendElement |
| f56 HashMap.HashMap<int64, int64>.insert | 2 | std/collections/hash-map.nra:441:17 | 200,000 | 73.1% | call (through a dispatch block) → HashCore.HashCore.is_full |
| f37 HashMap.HashMap<int64, int64>.get_or | 4 | std/collections/hash-map.nra:71:25 | 200,000 | 74.7% | inline ReadTag: no exit stays in the region (successor: call) |
| f37 HashMap.HashMap<int64, int64>.get_or | 2 | std/collections/hash-map.nra:71:42 | 200,000 | 76.2% | call (closure, through a dispatch block) → $closure1243<int64>.$invoke |
| f37 HashMap.HashMap<int64, int64>.get_or | 3 | std/collections/hash-map.nra:71:35 | 200,000 | 77.8% | call (direct) → HashMap.spread |
| f37 HashMap.HashMap<int64, int64>.get_or | 5 | std/collections/hash-map.nra:71:25 | 200,000 | 79.3% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.find |
| f37 HashMap.HashMap<int64, int64>.get_or | 13 | std/collections/hash-map.nra:75:13 | 200,000 | 80.9% | return |
| f1 HashMapBenchmark.main | 29 | hash-map.nra:33:9 | 150,001 | 82.1% | call (through a dispatch block) → HashMap.HashMapIterator<int64, int64>.has_next |
| f53 HashMap.HashMapIterator<int64, int64>.next | 15 | std/collections/hash-map.nra:517:13 | 150,000 | 83.2% | return |
| f53 HashMap.HashMapIterator<int64, int64>.next | 10 | std/collections/hash-map.nra:515:33 | 150,000 | 84.4% | object allocation |
| f53 HashMap.HashMapIterator<int64, int64>.next | 1 | std/collections/hash-map.nra:512:18 | 150,000 | 85.6% | call (through a dispatch block) → HashMap.HashMapIterator<int64, int64>.has_next |
| f1 HashMapBenchmark.main | 33 | hash-map.nra:33:9 | 150,000 | 86.7% | call (through a dispatch block) → HashMap.HashMapIterator<int64, int64>.next |
| f53 HashMap.HashMapIterator<int64, int64>.next | 0 | std/collections/hash-map.nra:512:18 | 150,000 | 87.9% | inline ReadTag: no exit stays in the region (successor: call) |
| f36 HashMap.HashMap<int64, int64>.set | 13 | std/collections/hash-map.nra:93:17 | 100,000 | 88.7% | return |
| f1 HashMapBenchmark.main | 20 | hash-map.nra:27:13 | 100,000 | 89.5% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.set |
| f38 HashMap.HashMap<int64, int64>.delete | 10 | std/collections/hash-map.nra:119:13 | 50,000 | 89.8% | return |
| f1 HashMapBenchmark.main | 25 | hash-map.nra:30:13 | 50,000 | 90.2% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.delete |
| f57 HashMap.HashMap<int64, int64>.release | 10 | std/collections/hash-map.nra:453:13 | 50,000 | 90.6% | return |
| f70 HashCore.HashCore.needs_compaction | 6 | std/collections/internal/hash-core.nra:164:13 | 50,000 | 91.0% | return |
| f38 HashMap.HashMap<int64, int64>.delete | 5 | std/collections/hash-map.nra:114:25 | 50,000 | 91.4% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.find |
| f38 HashMap.HashMap<int64, int64>.delete | 9 | std/collections/hash-map.nra:118:13 | 50,000 | 91.8% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.release |
| f68 HashCore.HashCore.release | 16 | std/collections/internal/hash-core.nra:157:13 | 50,000 | 92.2% | return |
| f38 HashMap.HashMap<int64, int64>.delete | 2 | std/collections/hash-map.nra:114:42 | 50,000 | 92.6% | call (closure, through a dispatch block) → $closure1243<int64>.$invoke |
| f57 HashMap.HashMap<int64, int64>.release | 4 | std/collections/hash-map.nra:452:13 | 50,000 | 93.0% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.shorten |
| f38 HashMap.HashMap<int64, int64>.delete | 3 | std/collections/hash-map.nra:114:35 | 50,000 | 93.3% | call (direct) → HashMap.spread |
| f38 HashMap.HashMap<int64, int64>.delete | 4 | std/collections/hash-map.nra:114:25 | 50,000 | 93.7% | inline ReadTag: no exit stays in the region (successor: call) |
| f57 HashMap.HashMap<int64, int64>.release | 2 | std/collections/hash-map.nra:452:21 | 50,000 | 94.1% | call (through a dispatch block) → HashCore.HashCore.release |
| f57 HashMap.HashMap<int64, int64>.release | 3 | std/collections/hash-map.nra:452:13 | 50,000 | 94.5% | inline ReadTag: no exit stays in the region (successor: call) |
| f69 HashMap.HashMap<int64, int64>.shorten | 5 | std/collections/hash-map.nra:461:13 | 50,000 | 94.9% | return |
| f57 HashMap.HashMap<int64, int64>.release | 7 | std/collections/hash-map.nra:453:17 | 50,000 | 95.3% | call (through a dispatch block) → HashCore.HashCore.needs_compaction |

### mandelbrot (size 80)

Steps 2,592,328 (block steps; kernel-charged steps are not blocks); in regions 2,528,137 (97.5%); outside 64,191 (2.5%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| continue: region not formed yet (counting) | 58,797 | 91.6% |
| inline ReadGlobal: region not formed yet (formed later) | 3,999 | 6.2% |
| inline ReadGlobal: region not formed yet (counting) | 613 | 1.0% |
| block not lifted (below threshold) | 234 | 0.4% |
| inline ReadField: region not formed yet (counting) | 217 | 0.3% |
| return | 56 | 0.1% |
| (other categories) | 275 | 0.4% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f1 Mandelbrot.main | 15 | mandelbrot.nra:29:28 | 3,999 | 6.2% | inline ReadGlobal: region not formed yet (formed later) |
| f1 Mandelbrot.main | 16 | mandelbrot.nra:29:28 | 3,995 | 12.5% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 17 | mandelbrot.nra:29:24 | 3,995 | 18.7% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 19 | mandelbrot.nra:29:42 | 3,989 | 24.9% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 18 | mandelbrot.nra:29:42 | 3,989 | 31.1% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 29 | mandelbrot.nra:33:21 | 3,385 | 36.4% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 24 | mandelbrot.nra:31:21 | 3,385 | 41.7% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 27 | mandelbrot.nra:32:21 | 3,385 | 46.9% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 30 | mandelbrot.nra:33:26 | 3,381 | 52.2% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 25 | mandelbrot.nra:31:26 | 3,381 | 57.5% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 31 | mandelbrot.nra:34:25 | 3,381 | 62.7% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 22 | mandelbrot.nra:30:26 | 3,381 | 68.0% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 28 | mandelbrot.nra:32:26 | 3,381 | 73.3% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 20 | mandelbrot.nra:30:21 | 3,381 | 78.5% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 23 | mandelbrot.nra:30:26 | 3,381 | 83.8% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 26 | mandelbrot.nra:31:26 | 3,381 | 89.1% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 10 | mandelbrot.nra:22:13 | 618 | 90.0% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 21 | mandelbrot.nra:36:17 | 614 | 91.0% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 14 | mandelbrot.nra:24:17 | 611 | 91.9% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 13 | mandelbrot.nra:23:26 | 611 | 92.9% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 11 | mandelbrot.nra:23:17 | 611 | 93.8% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 33 | mandelbrot.nra:37:21 | 610 | 94.8% | continue: region not formed yet (counting) |
| f1 Mandelbrot.main | 12 | mandelbrot.nra:22:40 | 610 | 95.7% | continue: region not formed yet (counting) |

### matrix-multiply (size 90)

Steps 6,052,071 (block steps; kernel-charged steps are not blocks); in regions 5,906,954 (97.6%); outside 145,117 (2.4%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| continue: region not formed yet (counting) | 67,424 | 46.5% |
| state op AppendElement | 24,325 | 16.8% |
| continue: region not formed yet (formed later) | 17,996 | 12.4% |
| inline ReadElement: region not formed yet (counting) | 16,859 | 11.6% |
| inline WriteElement: region not formed yet (counting) | 15,907 | 11.0% |
| inline ReadGlobal: region not formed yet (counting) | 1,002 | 0.7% |
| continue: no exit stays in the region (successor: AppendElement) | 1,002 | 0.7% |
| (other categories) | 602 | 0.4% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f4 Lists.filled<int64> | 5 | std/collections/lists.nra:114:13 | 24,300 | 16.7% | state op AppendElement |
| f1 MatrixMultiply.main | 17 | matrix-multiply.nra:20:13 | 7,999 | 22.3% | continue: region not formed yet (formed later) |
| f1 MatrixMultiply.main | 30 | matrix-multiply.nra:28:17 | 7,999 | 27.8% | continue: region not formed yet (formed later) |
| f1 MatrixMultiply.main | 22 | matrix-multiply.nra:22:19 | 7,912 | 33.2% | inline WriteElement: region not formed yet (counting) |
| f1 MatrixMultiply.main | 36 | matrix-multiply.nra:29:33 | 7,912 | 38.7% | continue: region not formed yet (counting) |
| f1 MatrixMultiply.main | 32 | matrix-multiply.nra:29:35 | 7,912 | 44.1% | continue: region not formed yet (counting) |
| f1 MatrixMultiply.main | 18 | matrix-multiply.nra:20:40 | 7,912 | 49.6% | continue: region not formed yet (counting) |
| f1 MatrixMultiply.main | 21 | matrix-multiply.nra:22:19 | 7,912 | 55.0% | continue: region not formed yet (counting) |
| f1 MatrixMultiply.main | 19 | matrix-multiply.nra:21:19 | 7,912 | 60.5% | continue: region not formed yet (counting) |
| f1 MatrixMultiply.main | 20 | matrix-multiply.nra:21:19 | 7,912 | 65.9% | inline WriteElement: region not formed yet (counting) |
| f1 MatrixMultiply.main | 31 | matrix-multiply.nra:28:44 | 7,912 | 71.4% | continue: region not formed yet (counting) |
| f1 MatrixMultiply.main | 34 | matrix-multiply.nra:29:50 | 7,912 | 76.8% | continue: region not formed yet (counting) |
| f1 MatrixMultiply.main | 37 | matrix-multiply.nra:29:27 | 7,912 | 82.3% | continue: region not formed yet (counting) |
| f1 MatrixMultiply.main | 33 | matrix-multiply.nra:29:35 | 7,912 | 87.7% | inline ReadElement: region not formed yet (counting) |
| f1 MatrixMultiply.main | 35 | matrix-multiply.nra:29:50 | 7,912 | 93.2% | inline ReadElement: region not formed yet (counting) |
| f4 Lists.filled<int64> | 4 | std/collections/lists.nra:113:9 | 1,002 | 93.9% | continue: no exit stays in the region (successor: AppendElement) |
| f1 MatrixMultiply.main | 46 | matrix-multiply.nra:36:49 | 999 | 94.6% | continue: region not formed yet (counting) |
| f1 MatrixMultiply.main | 45 | matrix-multiply.nra:36:24 | 999 | 95.3% | inline ReadGlobal: region not formed yet (counting) |

### nbody (size 2500)

Steps 2,492,170 (block steps; kernel-charged steps are not blocks); in regions 2,365,183 (94.9%); outside 126,987 (5.1%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| continue: region not formed yet (counting) | 38,342 | 30.2% |
| inline ReadField: region not formed yet (counting) | 25,501 | 20.1% |
| kernel neura.math.sqrt | 25,020 | 19.7% |
| inline ReadElement: region not formed yet (counting) | 18,111 | 14.3% |
| inline WriteElement: region not formed yet (counting) | 6,951 | 5.5% |
| return | 2,575 | 2.0% |
| call (through a dispatch block) | 2,500 | 2.0% |
| inline ReadTag: no exit stays in the region (successor: call) | 2,500 | 2.0% |
| inline ReadGlobal: region not formed yet (formed later) | 1,998 | 1.6% |
| continue: region not formed yet (formed later) | 1,998 | 1.6% |
| block not lifted (below threshold) | 716 | 0.6% |
| inline ReadGlobal: region not formed yet (counting) | 450 | 0.4% |
| (other categories) | 325 | 0.3% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f25 NBody.Planets.advance | 30 | nbody.nra:60:36 | 25,000 | 19.7% | kernel neura.math.sqrt |
| f25 NBody.Planets.advance | 91 | nbody.nra:70:13 | 2,500 | 21.7% | return |
| f1 NBody.main | 12 | nbody.nra:98:13 | 2,500 | 23.6% | call (through a dispatch block) → NBody.Planets.advance |
| f1 NBody.main | 9 | nbody.nra:98:13 | 2,500 | 25.6% | inline ReadTag: no exit stays in the region (successor: call) |
| f1 NBody.main | 8 | nbody.nra:97:9 | 2,497 | 27.6% | continue: region not formed yet (counting) |
| f1 NBody.main | 11 | nbody.nra:97:36 | 2,496 | 29.5% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 82 | nbody.nra:67:42 | 999 | 30.3% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 80 | nbody.nra:67:29 | 999 | 31.1% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 6 | nbody.nra:55:41 | 999 | 31.9% | inline ReadGlobal: region not formed yet (formed later) |
| f25 NBody.Planets.advance | 0 | nbody.nra:54:13 | 999 | 32.7% | continue: region not formed yet (formed later) |
| f25 NBody.Planets.advance | 79 | nbody.nra:67:21 | 999 | 33.5% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 34 | nbody.nra:62:21 | 999 | 34.2% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 35 | nbody.nra:62:29 | 999 | 35.0% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 37 | nbody.nra:62:42 | 999 | 35.8% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 43 | nbody.nra:63:21 | 999 | 36.6% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 44 | nbody.nra:63:29 | 999 | 37.4% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 46 | nbody.nra:63:42 | 999 | 38.2% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 52 | nbody.nra:64:21 | 999 | 39.0% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 53 | nbody.nra:64:29 | 999 | 39.8% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 55 | nbody.nra:64:42 | 999 | 40.5% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 61 | nbody.nra:65:21 | 999 | 41.3% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 64 | nbody.nra:65:42 | 999 | 42.1% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 70 | nbody.nra:66:21 | 999 | 42.9% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 71 | nbody.nra:66:29 | 999 | 43.7% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 73 | nbody.nra:66:42 | 999 | 44.5% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 1 | nbody.nra:54:33 | 999 | 45.3% | inline ReadGlobal: region not formed yet (formed later) |
| f25 NBody.Planets.advance | 31 | nbody.nra:61:21 | 999 | 46.0% | continue: region not formed yet (formed later) |
| f25 NBody.Planets.advance | 62 | nbody.nra:65:29 | 999 | 46.8% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 60 | nbody.nra:64:21 | 995 | 47.6% | inline WriteElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 58 | nbody.nra:64:37 | 995 | 48.4% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 51 | nbody.nra:63:21 | 995 | 49.2% | inline WriteElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 59 | nbody.nra:64:29 | 995 | 50.0% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 76 | nbody.nra:66:37 | 995 | 50.8% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 50 | nbody.nra:63:29 | 995 | 51.5% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 39 | nbody.nra:62:37 | 995 | 52.3% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 38 | nbody.nra:62:42 | 995 | 53.1% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 40 | nbody.nra:62:37 | 995 | 53.9% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 49 | nbody.nra:63:37 | 995 | 54.7% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 72 | nbody.nra:66:29 | 995 | 55.5% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 78 | nbody.nra:66:21 | 995 | 56.2% | inline WriteElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 81 | nbody.nra:67:29 | 995 | 57.0% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 83 | nbody.nra:67:42 | 995 | 57.8% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 65 | nbody.nra:65:42 | 995 | 58.6% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 84 | nbody.nra:67:37 | 995 | 59.4% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 74 | nbody.nra:66:42 | 995 | 60.2% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 86 | nbody.nra:67:29 | 995 | 60.9% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 47 | nbody.nra:63:42 | 995 | 61.7% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 48 | nbody.nra:63:37 | 995 | 62.5% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 75 | nbody.nra:66:37 | 995 | 63.3% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 77 | nbody.nra:66:29 | 995 | 64.1% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 7 | nbody.nra:55:41 | 995 | 64.9% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 32 | nbody.nra:61:42 | 995 | 65.6% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 33 | nbody.nra:61:37 | 995 | 66.4% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 36 | nbody.nra:62:29 | 995 | 67.2% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 41 | nbody.nra:62:29 | 995 | 68.0% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 42 | nbody.nra:62:21 | 995 | 68.8% | inline WriteElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 45 | nbody.nra:63:29 | 995 | 69.6% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 54 | nbody.nra:64:29 | 995 | 70.3% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 56 | nbody.nra:64:42 | 995 | 71.1% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 57 | nbody.nra:64:37 | 995 | 71.9% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 63 | nbody.nra:65:29 | 995 | 72.7% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 66 | nbody.nra:65:37 | 995 | 73.5% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 67 | nbody.nra:65:37 | 995 | 74.3% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 68 | nbody.nra:65:29 | 995 | 75.0% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 69 | nbody.nra:65:21 | 995 | 75.8% | inline WriteElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 85 | nbody.nra:67:37 | 995 | 76.6% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 87 | nbody.nra:67:21 | 995 | 77.4% | inline WriteElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 9 | nbody.nra:55:53 | 995 | 78.2% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 2 | nbody.nra:54:33 | 995 | 79.0% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 3 | nbody.nra:55:30 | 929 | 79.7% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 16 | nbody.nra:57:37 | 667 | 80.2% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 14 | nbody.nra:57:30 | 667 | 80.7% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 11 | nbody.nra:56:37 | 667 | 81.3% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 8 | nbody.nra:56:30 | 667 | 81.8% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 24 | nbody.nra:59:21 | 667 | 82.3% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 21 | nbody.nra:58:37 | 667 | 82.8% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 19 | nbody.nra:58:30 | 667 | 83.4% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 10 | nbody.nra:56:30 | 663 | 83.9% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 12 | nbody.nra:56:37 | 663 | 84.4% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 13 | nbody.nra:56:30 | 663 | 84.9% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 17 | nbody.nra:57:37 | 663 | 85.5% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 18 | nbody.nra:57:30 | 663 | 86.0% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 20 | nbody.nra:58:30 | 663 | 86.5% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 22 | nbody.nra:58:37 | 663 | 87.0% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 23 | nbody.nra:58:30 | 663 | 87.5% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 25 | nbody.nra:59:35 | 663 | 88.1% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 26 | nbody.nra:59:45 | 663 | 88.6% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 27 | nbody.nra:59:35 | 663 | 89.1% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 28 | nbody.nra:59:55 | 663 | 89.6% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 29 | nbody.nra:59:35 | 663 | 90.2% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 15 | nbody.nra:57:30 | 663 | 90.7% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 88 | nbody.nra:70:33 | 396 | 91.0% | inline ReadGlobal: region not formed yet (counting) |
| f25 NBody.Planets.advance | 89 | nbody.nra:70:33 | 392 | 91.3% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 90 | nbody.nra:71:17 | 330 | 91.6% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 93 | nbody.nra:71:24 | 330 | 91.8% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 100 | nbody.nra:72:17 | 330 | 92.1% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 108 | nbody.nra:73:17 | 330 | 92.3% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 95 | nbody.nra:71:36 | 330 | 92.6% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 103 | nbody.nra:72:36 | 330 | 92.9% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 111 | nbody.nra:73:36 | 330 | 93.1% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 101 | nbody.nra:72:24 | 330 | 93.4% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 109 | nbody.nra:73:24 | 330 | 93.6% | inline ReadField: region not formed yet (counting) |
| f25 NBody.Planets.advance | 5 | nbody.nra:54:45 | 328 | 93.9% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 92 | nbody.nra:70:45 | 326 | 94.1% | continue: region not formed yet (counting) |
| f25 NBody.Planets.advance | 96 | nbody.nra:71:36 | 326 | 94.4% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 99 | nbody.nra:71:17 | 326 | 94.7% | inline WriteElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 104 | nbody.nra:72:36 | 326 | 94.9% | inline ReadElement: region not formed yet (counting) |
| f25 NBody.Planets.advance | 107 | nbody.nra:72:17 | 326 | 95.2% | inline WriteElement: region not formed yet (counting) |

### object-churn (size 1000)

Steps 57,220,430 (block steps; kernel-charged steps are not blocks); in regions 35,856,924 (62.7%); outside 21,363,506 (37.3%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 7,578,308 | 35.5% |
| call (through a dispatch block) | 7,232,248 | 33.9% |
| state op AppendElement | 2,238,460 | 10.5% |
| inline ReadField: no exit stays in the region (successor: AppendElement) | 2,238,432 | 10.5% |
| inline ReadTag: no exit stays in the region (successor: call) | 1,339,711 | 6.3% |
| call (direct) | 346,058 | 1.6% |
| object allocation | 344,015 | 1.6% |
| (other categories) | 46,274 | 0.2% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f27 ArrayList.ArrayList<Particle>.size | 2 | std/collections/array-list.nra:30:42 | 2,328,409 | 10.9% | return |
| f29 ArrayList.ArrayList<Particle>.add | 0 | std/collections/array-list.nra:79:13 | 2,238,432 | 21.4% | inline ReadField: no exit stays in the region (successor: AppendElement) |
| f29 ArrayList.ArrayList<Particle>.add | 1 | std/collections/array-list.nra:79:13 | 2,238,432 | 31.9% | state op AppendElement |
| f29 ArrayList.ArrayList<Particle>.add | 2 | std/collections/array-list.nra:80:13 | 2,238,432 | 42.3% | return |
| f28 ArrayList.ArrayList<Particle>.get | 2 | std/collections/array-list.nra:36:51 | 2,237,408 | 52.8% | return |
| f1 ObjectChurn.main | 46 | object-churn.nra:59:33 | 1,247,704 | 58.6% | call (through a dispatch block) → ArrayList.ArrayList<Particle>.size |
| f1 ObjectChurn.main | 51 | object-churn.nra:60:33 | 1,246,704 | 64.5% | call (through a dispatch block) → ArrayList.ArrayList<Particle>.get |
| f1 ObjectChurn.main | 61 | object-churn.nra:63:21 | 991,728 | 69.1% | call (through a dispatch block) → ArrayList.ArrayList<Particle>.add |
| f1 ObjectChurn.main | 13 | object-churn.nra:47:33 | 991,704 | 73.8% | call (through a dispatch block) → ArrayList.ArrayList<Particle>.size |
| f1 ObjectChurn.main | 18 | object-churn.nra:48:32 | 990,704 | 78.4% | call (through a dispatch block) → ArrayList.ArrayList<Particle>.get |
| f1 ObjectChurn.main | 20 | object-churn.nra:48:17 | 990,704 | 83.0% | call (through a dispatch block) → ArrayList.ArrayList<Particle>.add |
| f1 ObjectChurn.main | 19 | object-churn.nra:48:17 | 990,704 | 87.7% | inline ReadTag: no exit stays in the region (successor: call) |
| f30 ObjectChurn.Random.next | 3 | object-churn.nra:36:13 | 342,000 | 89.3% | return |
| f6 ObjectChurn.Particle.init | 3 | object-churn.nra:27:13 | 342,000 | 90.9% | return |
| f1 ObjectChurn.main | 28 | object-churn.nra:52:45 | 256,000 | 92.1% | call (through a dispatch block) → ObjectChurn.Random.next |
| f1 ObjectChurn.main | 29 | object-churn.nra:52:32 | 256,000 | 93.3% | object allocation |
| f1 ObjectChurn.main | 31 | object-churn.nra:52:17 | 256,000 | 94.5% | inline ReadTag: no exit stays in the region (successor: call) |
| f1 ObjectChurn.main | 32 | object-churn.nra:52:17 | 256,000 | 95.7% | call (through a dispatch block) → ArrayList.ArrayList<Particle>.add |

### parse-results (size 500)

Steps 44,722,855 (block steps; kernel-charged steps are not blocks); in regions 39,735,132 (88.8%); outside 4,987,723 (11.2%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 1,532,987 | 30.7% |
| call (direct) | 1,532,925 | 30.7% |
| object allocation | 1,381,019 | 27.7% |
| text building (TextSlice) | 290,506 | 5.8% |
| inline WriteField: no exit stays in the region (successor: return) | 109,500 | 2.2% |
| continue: region not formed yet (counting) | 39,038 | 0.8% |
| state op AppendElement | 18,621 | 0.4% |
| inline ReadField: region not formed yet (formed later) | 16,994 | 0.3% |
| continue: region not formed yet (formed later) | 12,993 | 0.3% |
| inline WriteField: region not formed yet (counting) | 11,081 | 0.2% |
| inline WriteField: region not formed yet (formed later) | 8,991 | 0.2% |
| inline ReadElement: region not formed yet (counting) | 5,426 | 0.1% |
| inline ReadLength: region not formed yet (counting) | 5,237 | 0.1% |
| (other categories) | 22,405 | 0.4% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f1 ParseResults.main | 51 | parse-results.nra:129:24 | 500,000 | 10.0% | call (direct) → ParseResults.separator_of |
| f1 ParseResults.main | 57 | parse-results.nra:134:56 | 440,500 | 18.9% | call (direct) → ParseResults.parse_int |
| f5 ParseResults.separator_of | 9 | parse-results.nra:28:24 | 440,500 | 27.7% | object allocation |
| f5 ParseResults.separator_of | 14 | parse-results.nra:28:17 | 440,500 | 36.5% | return |
| f1 ParseResults.main | 63 | parse-results.nra:136:40 | 290,500 | 42.3% | call (direct) → ParseResults.key_weight |
| f1 ParseResults.main | 62 | parse-results.nra:136:51 | 290,500 | 48.2% | text building (TextSlice) |
| f6 ParseResults.parse_int | 20 | parse-results.nra:65:16 | 237,000 | 52.9% | object allocation |
| f6 ParseResults.parse_int | 26 | parse-results.nra:65:9 | 237,000 | 57.7% | return |
| f1 ParseResults.main | 72 | parse-results.nra:147:43 | 150,000 | 60.7% | call (direct) → ParseResults.error_offset |
| f11 ParseResults.parse_error | 8 | parse-results.nra:35:9 | 150,000 | 63.7% | return |
| f11 ParseResults.parse_error | 0 | parse-results.nra:35:41 | 150,000 | 66.7% | object allocation |
| f11 ParseResults.parse_error | 5 | parse-results.nra:35:16 | 150,000 | 69.7% | object allocation |
| f8 ParseResults.error_offset | 4 | parse-results.nra:88:17 | 150,000 | 72.7% | return |
| f7 ParseResults.key_weight | 12 | parse-results.nra:74:13 | 62,000 | 74.0% | return |
| f7 ParseResults.key_weight | 8 | parse-results.nra:74:20 | 62,000 | 75.2% | object allocation |
| f7 ParseResults.key_weight | 6 | parse-results.nra:71:13 | 61,000 | 76.4% | return |
| f7 ParseResults.key_weight | 2 | parse-results.nra:71:20 | 61,000 | 77.6% | object allocation |
| f5 ParseResults.separator_of | 4 | parse-results.nra:31:16 | 59,500 | 78.8% | object allocation |
| f5 ParseResults.separator_of | 15 | parse-results.nra:31:16 | 59,500 | 80.0% | inline WriteField: no exit stays in the region (successor: return) |
| f5 ParseResults.separator_of | 16 | parse-results.nra:31:9 | 59,500 | 81.2% | return |
| f7 ParseResults.key_weight | 18 | parse-results.nra:77:13 | 59,000 | 82.4% | return |
| f7 ParseResults.key_weight | 14 | parse-results.nra:77:20 | 59,000 | 83.6% | object allocation |
| f7 ParseResults.key_weight | 24 | parse-results.nra:80:13 | 58,500 | 84.8% | return |
| f6 ParseResults.parse_int | 16 | parse-results.nra:57:24 | 58,500 | 85.9% | call (direct) → ParseResults.parse_error |
| f6 ParseResults.parse_int | 18 | parse-results.nra:57:17 | 58,500 | 87.1% | return |
| f7 ParseResults.key_weight | 20 | parse-results.nra:80:20 | 58,500 | 88.3% | object allocation |
| f6 ParseResults.parse_int | 19 | parse-results.nra:63:44 | 53,500 | 89.3% | object allocation |
| f6 ParseResults.parse_int | 23 | parse-results.nra:63:13 | 53,500 | 90.4% | return |
| f7 ParseResults.key_weight | 26 | parse-results.nra:82:9 | 50,000 | 91.4% | return |
| f7 ParseResults.key_weight | 21 | parse-results.nra:82:16 | 50,000 | 92.4% | object allocation |
| f7 ParseResults.key_weight | 25 | parse-results.nra:82:16 | 50,000 | 93.4% | inline WriteField: no exit stays in the region (successor: return) |
| f6 ParseResults.parse_int | 6 | parse-results.nra:48:20 | 49,500 | 94.4% | call (direct) → ParseResults.parse_error |
| f6 ParseResults.parse_int | 8 | parse-results.nra:48:13 | 49,500 | 95.4% | return |

### sha256 (size 500000)

Steps 31,343,491 (block steps; kernel-charged steps are not blocks); in regions 6,463,467 (20.6%); outside 24,880,024 (79.4%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 9,196,300 | 37.0% |
| call (direct) | 9,196,078 | 37.0% |
| inline ReadGlobal: no exit stays in the region (successor: return) | 4,687,796 | 18.8% |
| inline ReadElement: no exit stays in the region (successor: call) | 1,250,068 | 5.0% |
| state op AppendElement | 500,361 | 2.0% |
| (other categories) | 49,421 | 0.2% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f11 Sha256.add | 1 | sha256.nra:29:55 | 4,687,800 | 18.8% | return |
| f11 Sha256.add | 0 | sha256.nra:29:90 | 4,687,796 | 37.7% | inline ReadGlobal: no exit stays in the region (successor: return) |
| f10 Sha256.rotate | 0 | sha256.nra:26:63 | 4,500,288 | 55.8% | return |
| f5 Sha256.compress | 38 | sha256.nra:56:30 | 500,032 | 57.8% | call (direct) → Sha256.add |
| f5 Sha256.compress | 45 | sha256.nra:57:22 | 500,032 | 59.8% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 41 | sha256.nra:56:42 | 500,032 | 61.8% | call (direct) → Sha256.add |
| f5 Sha256.compress | 50 | sha256.nra:67:17 | 500,032 | 63.8% | call (direct) → Sha256.add |
| f5 Sha256.compress | 33 | sha256.nra:54:22 | 500,032 | 65.8% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 49 | sha256.nra:63:17 | 500,032 | 67.8% | call (direct) → Sha256.add |
| f5 Sha256.compress | 48 | sha256.nra:59:22 | 500,032 | 69.8% | call (direct) → Sha256.add |
| f5 Sha256.compress | 46 | sha256.nra:57:37 | 500,032 | 71.8% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 47 | sha256.nra:57:53 | 500,032 | 73.9% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 44 | sha256.nra:56:22 | 500,032 | 75.9% | call (direct) → Sha256.add |
| f5 Sha256.compress | 42 | sha256.nra:56:26 | 500,032 | 77.9% | call (direct) → Sha256.add |
| f5 Sha256.compress | 36 | sha256.nra:54:37 | 500,032 | 79.9% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 37 | sha256.nra:54:53 | 500,032 | 81.9% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 43 | sha256.nra:56:75 | 500,028 | 83.9% | inline ReadElement: no exit stays in the region (successor: call) |
| f1 Sha256.main | 6 | sha256.nra:83:28 | 500,000 | 85.9% | state op AppendElement |
| f5 Sha256.compress | 22 | sha256.nra:43:27 | 375,024 | 87.4% | call (direct) → Sha256.add |
| f5 Sha256.compress | 19 | sha256.nra:43:31 | 375,024 | 88.9% | call (direct) → Sha256.add |
| f5 Sha256.compress | 16 | sha256.nra:42:22 | 375,024 | 90.4% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 21 | sha256.nra:43:58 | 375,024 | 91.9% | call (direct) → Sha256.add |
| f5 Sha256.compress | 14 | sha256.nra:41:22 | 375,024 | 93.5% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 15 | sha256.nra:41:39 | 375,024 | 95.0% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 17 | sha256.nra:42:39 | 375,024 | 96.5% | call (direct) → Sha256.rotate |

### sieve (size 3000000)

Steps 41,695,696 (block steps; kernel-charged steps are not blocks); in regions 38,664,445 (92.7%); outside 3,031,251 (7.3%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| state op AppendElement | 3,000,026 | 99.0% |
| continue: region not formed yet (counting) | 10,578 | 0.3% |
| continue: region not formed yet (formed later) | 9,997 | 0.3% |
| (other categories) | 10,650 | 0.4% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f4 Lists.filled<bool> | 5 | std/collections/lists.nra:114:13 | 3,000,001 | 99.0% | state op AppendElement |

### sort (size 250000)

Steps 73,316,797 (block steps; kernel-charged steps are not blocks); in regions 55,874,260 (76.2%); outside 17,442,537 (23.8%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 8,382,530 | 48.1% |
| call (direct) | 4,199,105 | 24.1% |
| call (closure, through a dispatch block) | 4,183,423 | 24.0% |
| state op AppendElement | 500,014 | 2.9% |
| continue: region not formed yet (counting) | 88,755 | 0.5% |
| continue: region not formed yet (formed later) | 37,990 | 0.2% |
| (other categories) | 50,720 | 0.3% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f32 $closure779.$invoke | 0 | sort.nra:23:36 | 4,183,423 | 24.0% | call (direct) → Int32.compare |
| f32 $closure779.$invoke | 1 | sort.nra:23:36 | 4,183,423 | 48.0% | return |
| f25 Sorting.merge_pass<int32> | 28 | std/collections/sorting.nra:84:34 | 3,464,975 | 67.8% | call (closure, through a dispatch block) → $closure779.$invoke |
| f35 Int32.compare | 3 | std/core/int32.nra:69:13 | 2,125,382 | 80.0% | return |
| f35 Int32.compare | 1 | std/core/int32.nra:66:13 | 2,028,202 | 91.6% | return |
| f17 Sorting.insertion_sort<int32> | 9 | std/collections/sorting.nra:26:30 | 702,824 | 95.7% | call (closure, through a dispatch block) → $closure779.$invoke |

### spectral-norm (size 64)

Steps 1,653,450 (block steps; kernel-charged steps are not blocks); in regions 1,309,334 (79.2%); outside 344,116 (20.8%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 163,931 | 47.6% |
| call (direct) | 163,929 | 47.6% |
| continue: region not formed yet (counting) | 8,116 | 2.4% |
| continue: region not formed yet (formed later) | 4,995 | 1.5% |
| inline ReadElement: region not formed yet (formed later) | 1,998 | 0.6% |
| (other categories) | 1,147 | 0.3% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f9 SpectralNorm.entry | 2 | spectral-norm.nra:15:24 | 163,840 | 47.6% | return |
| f6 SpectralNorm.multiply_transposed | 6 | spectral-norm.nra:34:29 | 81,920 | 71.4% | call (direct) → SpectralNorm.entry |
| f5 SpectralNorm.multiply | 6 | spectral-norm.nra:23:29 | 81,920 | 95.2% | call (direct) → SpectralNorm.entry |

### string-building (size 400000)

Steps 63,578,690 (block steps; kernel-charged steps are not blocks); in regions 54,011,053 (85.0%); outside 9,567,637 (15.0%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| state op AppendElement | 4,688,924 | 49.0% |
| return | 1,600,061 | 16.7% |
| call (through a dispatch block) | 1,600,000 | 16.7% |
| inline ReadTag: no exit stays in the region (successor: call) | 1,208,003 | 12.6% |
| text building (ScalarText) | 400,000 | 4.2% |
| continue: region not formed yet (counting) | 25,045 | 0.3% |
| (other categories) | 45,604 | 0.5% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f27 StringBuilder.StringBuilder.append | 7 | std/core/string-builder.nra:35:17 | 4,688,924 | 49.0% | state op AppendElement |
| f27 StringBuilder.StringBuilder.append | 9 | std/core/string-builder.nra:37:13 | 1,200,005 | 61.6% | return |
| f28 StringBuilder.StringBuilder.append_int | 0 | std/core/string-builder.nra:86:75 | 400,000 | 65.7% | text building (ScalarText) |
| f28 StringBuilder.StringBuilder.append_int | 2 | std/core/string-builder.nra:86:68 | 400,000 | 69.9% | call (through a dispatch block) → StringBuilder.StringBuilder.append |
| f1 StringBuilding.main | 9 | string-building.nra:18:13 | 400,000 | 74.1% | call (through a dispatch block) → StringBuilder.StringBuilder.append |
| f1 StringBuilding.main | 13 | string-building.nra:18:13 | 400,000 | 78.3% | call (through a dispatch block) → StringBuilder.StringBuilder.append |
| f1 StringBuilding.main | 11 | string-building.nra:18:13 | 400,000 | 82.5% | call (through a dispatch block) → StringBuilder.StringBuilder.append_int |
| f1 StringBuilding.main | 10 | string-building.nra:18:13 | 400,000 | 86.6% | inline ReadTag: no exit stays in the region (successor: call) |
| f1 StringBuilding.main | 12 | string-building.nra:18:13 | 400,000 | 90.8% | inline ReadTag: no exit stays in the region (successor: call) |
| f28 StringBuilder.StringBuilder.append_int | 1 | std/core/string-builder.nra:86:68 | 400,000 | 95.0% | inline ReadTag: no exit stays in the region (successor: call) |
| f28 StringBuilder.StringBuilder.append_int | 3 | std/core/string-builder.nra:86:68 | 400,000 | 99.2% | return |

### text-split (size 15000)

Steps 22,014,063 (block steps; kernel-charged steps are not blocks); in regions 16,638,358 (75.6%); outside 5,375,705 (24.4%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| state op AppendElement | 2,050,473 | 38.1% |
| return | 990,295 | 18.4% |
| call (through a dispatch block) | 510,223 | 9.5% |
| call (direct) | 480,070 | 8.9% |
| inline ReadElement: no exit stays in the region (successor: AppendElement) | 358,458 | 6.7% |
| text building (TextSlice) | 240,004 | 4.5% |
| kernel neura.text.space_suffix_start | 120,000 | 2.2% |
| kernel neura.text.space_prefix_length | 120,000 | 2.2% |
| collection allocation (AllocateCollection) | 90,004 | 1.7% |
| inline ReadTag: no exit stays in the region (successor: call) | 90,000 | 1.7% |
| text building (ScalarText) | 90,000 | 1.7% |
| object allocation | 30,015 | 0.6% |
| inline WriteField: no exit stays in the region (successor: return) | 30,002 | 0.6% |
| text building (TextFromUtf8) | 30,001 | 0.6% |
| kernel neura.text.split | 30,000 | 0.6% |
| continue: region not formed yet (formed later) | 20,979 | 0.4% |
| continue: region not formed yet (counting) | 18,312 | 0.3% |
| inline ReadField: no exit stays in the region (successor: TextFromUtf8) | 15,001 | 0.3% |
| inline ReadLength: no exit stays in the region (successor: AllocateCollection) | 15,000 | 0.3% |
| continue: no exit stays in the region (successor: AllocateCollection) | 14,996 | 0.3% |
| inline ReadLength: region not formed yet (formed later) | 5,994 | 0.1% |
| (other categories) | 25,878 | 0.5% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f34 StringBuilder.StringBuilder.append | 7 | std/core/string-builder.nra:35:17 | 972,864 | 18.1% | state op AppendElement |
| f34 StringBuilder.StringBuilder.append | 9 | std/core/string-builder.nra:37:13 | 405,230 | 25.6% | return |
| f13 Strings.append_bytes | 14 | std/text/strings.nra:517:13 | 254,649 | 30.4% | state op AppendElement |
| f13 Strings.append_bytes | 13 | std/text/strings.nra:516:9 | 225,000 | 34.6% | return |
| f1 TextSplit.main | 21 | text-split.nra:35:21 | 120,223 | 36.8% | call (through a dispatch block) → StringBuilder.StringBuilder.append |
| f6 Strings.trim | 4 | std/text/strings.nra:241:16 | 120,000 | 39.0% | text building (TextSlice) |
| f1 TextSplit.main | 44 | text-split.nra:53:17 | 120,000 | 41.3% | state op AppendElement |
| f1 TextSplit.main | 40 | text-split.nra:49:32 | 120,000 | 43.5% | call (direct) → Strings.trim |
| f6 Strings.trim | 0 | std/text/strings.nra:236:20 | 120,000 | 45.7% | kernel neura.text.space_suffix_start |
| f6 Strings.trim | 1 | std/text/strings.nra:236:20 | 120,000 | 48.0% | kernel neura.text.space_prefix_length |
| f7 Strings.join | 16 | std/text/strings.nra:206:42 | 120,000 | 50.2% | call (direct) → Strings.append_bytes |
| f6 Strings.trim | 5 | std/text/strings.nra:241:9 | 120,000 | 52.4% | return |
| f13 Strings.append_bytes | 7 | std/text/strings.nra:512:13 | 119,490 | 54.6% | state op AppendElement |
| f13 Strings.append_bytes | 9 | std/text/strings.nra:513:13 | 119,490 | 56.9% | state op AppendElement |
| f13 Strings.append_bytes | 5 | std/text/strings.nra:511:13 | 119,490 | 59.1% | state op AppendElement |
| f13 Strings.append_bytes | 3 | std/text/strings.nra:510:13 | 119,490 | 61.3% | state op AppendElement |
| f13 Strings.append_bytes | 8 | std/text/strings.nra:513:36 | 119,486 | 63.5% | inline ReadElement: no exit stays in the region (successor: AppendElement) |
| f13 Strings.append_bytes | 6 | std/text/strings.nra:512:36 | 119,486 | 65.8% | inline ReadElement: no exit stays in the region (successor: AppendElement) |
| f13 Strings.append_bytes | 4 | std/text/strings.nra:511:36 | 119,486 | 68.0% | inline ReadElement: no exit stays in the region (successor: AppendElement) |
| f21 Strings.separators | 8 | std/text/strings.nra:464:13 | 105,000 | 69.9% | state op AppendElement |
| f12 Strings.split_pieces | 12 | std/text/strings.nra:476:9 | 105,000 | 71.9% | text building (TextSlice) |
| f7 Strings.join | 14 | std/text/strings.nra:203:51 | 105,000 | 73.8% | call (direct) → Strings.append_bytes |
| f12 Strings.split_pieces | 13 | std/text/strings.nra:477:13 | 105,000 | 75.8% | state op AppendElement |
| f1 TextSplit.main | 16 | text-split.nra:32:21 | 105,000 | 77.7% | call (through a dispatch block) → StringBuilder.StringBuilder.append |
| f1 TextSplit.main | 28 | text-split.nra:40:21 | 90,000 | 79.4% | call (through a dispatch block) → StringBuilder.StringBuilder.append_int |
| f35 StringBuilder.StringBuilder.append_int | 3 | std/core/string-builder.nra:86:68 | 90,000 | 81.1% | return |
| f35 StringBuilder.StringBuilder.append_int | 2 | std/core/string-builder.nra:86:68 | 90,000 | 82.8% | call (through a dispatch block) → StringBuilder.StringBuilder.append |
| f35 StringBuilder.StringBuilder.append_int | 1 | std/core/string-builder.nra:86:68 | 90,000 | 84.4% | inline ReadTag: no exit stays in the region (successor: call) |
| f35 StringBuilder.StringBuilder.append_int | 0 | std/core/string-builder.nra:86:75 | 90,000 | 86.1% | text building (ScalarText) |
| f1 TextSplit.main | 30 | text-split.nra:43:21 | 60,000 | 87.2% | call (through a dispatch block) → StringBuilder.StringBuilder.append |
| f1 TextSplit.main | 27 | text-split.nra:38:21 | 30,000 | 87.8% | call (through a dispatch block) → StringBuilder.StringBuilder.append |
| f20 Cell.Cell<StringBuilder>.init | 0 | std/core/cell.nra:14:13 | 15,001 | 88.1% | inline WriteField: no exit stays in the region (successor: return) |
| f4 StringBuilder.create | 1 | std/core/string-builder.nra:180:39 | 15,001 | 88.3% | object allocation |
| f11 StringBuilder.StringBuilder.init | 3 | std/core/string-builder.nra:28:13 | 15,001 | 88.6% | return |
| f10 Cell.of<StringBuilder> | 2 | std/core/cell.nra:19:53 | 15,001 | 88.9% | return |
| f10 Cell.of<StringBuilder> | 1 | std/core/cell.nra:19:53 | 15,001 | 89.2% | call (direct) → Cell.Cell<StringBuilder>.init |
| f36 StringBuilder.StringBuilder.to_string | 0 | std/core/string-builder.nra:159:50 | 15,001 | 89.5% | inline ReadField: no exit stays in the region (successor: TextFromUtf8) |
| f4 StringBuilder.create | 2 | std/core/string-builder.nra:180:39 | 15,001 | 89.7% | call (direct) → StringBuilder.StringBuilder.init |
| f36 StringBuilder.StringBuilder.to_string | 6 | std/core/string-builder.nra:163:13 | 15,001 | 90.0% | return |
| f4 StringBuilder.create | 4 | std/core/string-builder.nra:182:9 | 15,001 | 90.3% | return |
| f36 StringBuilder.StringBuilder.to_string | 1 | std/core/string-builder.nra:159:33 | 15,001 | 90.6% | text building (TextFromUtf8) |
| f10 Cell.of<StringBuilder> | 0 | std/core/cell.nra:19:53 | 15,001 | 90.9% | object allocation |
| f11 StringBuilder.StringBuilder.init | 0 | std/core/string-builder.nra:22:34 | 15,001 | 91.1% | collection allocation (AllocateCollection) |
| f20 Cell.Cell<StringBuilder>.init | 1 | std/core/cell.nra:14:13 | 15,001 | 91.4% | return |
| f4 StringBuilder.create | 0 | std/core/string-builder.nra:179:22 | 15,001 | 91.7% | call (direct) → Cell.of<StringBuilder> |
| f4 StringBuilder.create | 3 | std/core/string-builder.nra:181:9 | 15,001 | 92.0% | inline WriteField: no exit stays in the region (successor: return) |
| f1 TextSplit.main | 38 | text-split.nra:55:27 | 15,000 | 92.3% | call (direct) → Strings.join |
| f1 TextSplit.main | 33 | text-split.nra:47:41 | 15,000 | 92.5% | collection allocation (AllocateCollection) |
| f21 Strings.separators | 0 | std/text/strings.nra:457:71 | 15,000 | 92.8% | collection allocation (AllocateCollection) |
| f12 Strings.split_pieces | 2 | std/text/strings.nra:476:52 | 15,000 | 93.1% | call (direct) → Strings.separators |
| f12 Strings.split_pieces | 1 | std/text/strings.nra:473:18 | 15,000 | 93.4% | collection allocation (AllocateCollection) |
| f21 Strings.separators | 1 | std/text/strings.nra:457:22 | 15,000 | 93.7% | kernel neura.text.split |
| f12 Strings.split_pieces | 0 | std/text/strings.nra:473:18 | 15,000 | 93.9% | inline ReadLength: no exit stays in the region (successor: AllocateCollection) |
| f5 Strings.split | 1 | std/text/strings.nra:155:9 | 15,000 | 94.2% | return |
| f21 Strings.separators | 11 | std/text/strings.nra:468:9 | 15,000 | 94.5% | return |
| f1 TextSplit.main | 32 | text-split.nra:46:26 | 15,000 | 94.8% | call (direct) → Strings.split |
| f5 Strings.split | 0 | std/text/strings.nra:155:9 | 15,000 | 95.0% | call (direct) → Strings.split_pieces |


#!/usr/bin/env python3
"""Generate valid Neura programs and run each one under every execution mode and
under limit sweeps, so that a runtime change that alters anything observable
shows up as a difference with a reproduction.

The runtime contract (docs/runtime/NVM.md, "Execution tiers") is that one image
and one input give the same exit status, trap, steps, peak heap cells, standard
output, and profile at every tier and in every mode. Hand-written tests cover the
shapes someone thought of. This tool covers shapes nobody did:

    scripts/program-fuzz.py generate --seed 7 --out DIR
    scripts/program-fuzz.py run --neura build/neura --seeds 1..100 --jobs 4 --report DIR
    scripts/program-fuzz.py run --neura build/neura --quick

`generate` writes DIR/program-N.nra, a pure function of N (a `random.Random(N)`
and nothing else: no clock, no hashing, no iteration over unordered
containers). The first lines of a program are comments that name its arguments
(`// args: SIZE FAULTS`) and whether it runs tasks (`// tasks: yes`). Every
program reads its size from the first argument and a fault mask from the second:
with the mask zero a program runs to the end without a trap and prints a
checksum; each bit of the mask turns on one failure path (an overflow, an index
out of bounds, a read of an uninitialized field, a use after destroy, a slice
inside a character, a NaN converted to an integer, a division by zero, a
recursion past the frame limit, a trap in a task), so that the run ends in that
trap after the work before it.

`run` builds each program once, runs it at `--tier=0` for the reference
(`--stats`, `--profile`, exit status, standard output and error), and compares
every mode against the reference:

    jit off, jit on                            the default tier, regions interpreted or native
    NEURA_TIER_EAGER=1, jit off and on         every block lifted and every region formed at once
    each of the four with NEURA_TIER_PROGRAMS=1
    NEURA_TIER_EAGER=1 NEURA_TIER_CHECK=2 NEURA_TIER_PROGRAMS=1   the shadow oracle runs beside it
    NEURA_TIER3=interp NEURA_TIER_EAGER=1 NEURA_TIER3_VERIFY=1    function programs run as tier 3 IR on
                                               the IR interpreter, verified as they are built, without and
                                               with NEURA_TIER_CHECK=2

and then repeats the comparison under sweeps of limits that land a trap on every
block, so that a deoptimization or a trap in place is exercised at each of them:

    --max-steps         every value from 1 to the run's steps when that is at most
                        2000, otherwise 200 values: 1..200 and the last 200
    --max-heap-cells    from the smallest value that admits the first allocation to
                        peak_heap_cells + 2: every value when that is at most 500
                        values, otherwise 200 spread values
    --max-frames        from 1 to the run's deepest call depth + 2
    --task-quantum      1..4, for programs that run tasks

A difference is written to the report directory with the seed, the mode, the
limit, both outputs, and a shell command that reproduces it; the run goes on and
exits with status 1 at the end. A run that exceeds the time limit is a failure.
`--quick` runs a few seeds under coarse sweeps and is what CTest runs.
"""

import argparse
import concurrent.futures
import os
import random
import re
import shlex
import string
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

INTS = [
    ("int8", -128, 127, 8),
    ("int16", -32768, 32767, 16),
    ("int32", -2147483648, 2147483647, 32),
    ("int64", -9223372036854775808, 9223372036854775807, 64),
    ("uint8", 0, 255, 8),
    ("uint16", 0, 65535, 16),
    ("uint32", 0, 4294967295, 32),
    ("uint64", 0, 18446744073709551615, 64),
]

WORDS = ["alpha", "beta", "gamma", "delta", "été", "ñandú", "日本", "omega"]

# Every feature a program can use, in the order the coverage report lists them.
FEATURES = [
    "int-widths", "loops", "recursion", "mutual-recursion", "classes", "uninitialized-field", "destroy",
    "interfaces", "closures", "arrays", "lists", "index-fault", "text", "slice", "kernel", "floats", "nan",
    "tasks", "channels", "fault-path",
]


def fill(text, **values):
    """Substitute `$name` in a template; braces are left alone."""
    return string.Template(text).substitute(**values)


class Program:
    """One generated program: its header comments, imports, declarations, and the
    calls its main makes, built from a seeded random source."""

    def __init__(self, seed):
        self.seed = seed
        self.random = random.Random(seed)
        self.imports = []
        self.declarations = []
        self.calls = []
        self.features = []
        self.fault_bits = []
        self.tasks = False
        self.next_id = 1

    def use(self, *aliases):
        for alias in aliases:
            if alias not in self.imports:
                self.imports.append(alias)

    def feature(self, *names):
        for name in names:
            if name not in self.features:
                self.features.append(name)

    def fault(self, name):
        """Claims the next bit of the fault mask for a failure path."""
        bit = len(self.fault_bits)
        self.fault_bits.append(name)
        return bit

    def pick(self, items):
        return items[self.random.randrange(len(items))]

    def chance(self, percent):
        return self.random.randrange(100) < percent

    def between(self, low, high):
        return self.random.randint(low, high)


IMPORTS = {
    "console": "Std.Io.Console",
    "program": "Std.Program",
    "int32s": "Std.Core.Int32",
    "result": "Std.Core.Result",
    "channels": "Std.Concurrent.Channel",
    "tasks": "Std.Concurrent.Task",
    "sorting": "Std.Collections.Sorting",
    "crc32": "Std.Digest.Crc32",
    "sha256": "Std.Digest.Sha256",
    "math": "Std.Math.Math",
    "strings": "Std.Text.Strings",
    "builders": "Std.Core.StringBuilder",
    "lists": "Std.Collections.Lists",
}


# --- sections -------------------------------------------------------------------------------------------------------
# Each section adds declarations and one `sN_run(size, faults) -> int64` function, whose result main mixes into the
# checksum. A section prints intermediate results so that a divergence shows in the output.

def fault_test(bit):
    return f"on(faults, {bit})"


def section_integers(p, k):
    """Checked integer arithmetic at one width, kept in range by construction near the bounds, with an overflow
    on the fault path."""
    p.feature("int-widths")
    name, low, high, width = p.pick(INTS)
    signed = low < 0
    c = p.between(2, 5)
    span = 8
    modulus = (high - span) // c
    ops = []
    pool = [
        "if (i % $p == $q) {\n                acc = acc ^ (acc >> $s);\n            }",
        "acc = acc / ((i % 4 + 1) as $T);",
        "let edge: $T = $high - ((i % 3) as $T);\n            acc = acc / 2 + edge / 2;",
        "acc = acc - (acc % 3);",
        "acc = acc | 1;",
        "if (i % 11 == 10) {\n                acc = $high;\n            }",
    ]
    if signed:
        pool.append("if (acc > 0) {\n                acc = acc - 1;\n            }")
    else:
        pool.append("acc = (acc << $s) | (acc >> ($width - $s));")
    for text in p.random.sample(pool, p.between(2, 4)):
        ops.append(fill(text, p=p.between(3, 9), q=p.between(0, 2), s=p.between(1, 3), T=name, high=high,
                        width=width))
    body = "\n            ".join(ops)
    flavors = [
        fill("let top: $T = $high;\n        acc = top + (acc | 1);", T=name, high=high),
        fill("acc = (acc | 2) * $high;", high=high),
        "acc = acc / (acc - acc);",
        fill("let wide: int32 = $width;\n        acc = acc << wide;", width=width),
    ]
    if signed:
        flavors.append(fill("let low: $T = $low;\n        acc = low - (acc | 1);", T=name, low=low))
    else:
        flavors.append(fill("let zero: $T = 0;\n        acc = zero - (acc | 1);", T=name))
    if width > 8:
        flavors.append(fill("acc = ((acc | 256) as uint8) as $T;", T=name))
    flavor = p.pick(flavors)
    bit = p.fault("overflow-" + name)
    reduce = "(value % 1000000007) as int64" if name == "uint64" else "value as int64"
    p.declarations.append(fill("""let s${k}_arith = func(n: int32, faulty: bool) -> $T {
    let acc: $T = $init;
    for (let i: int32 = 0; i < n; i++) {
        acc = (acc % $M) * $C + ((i % $K) as $T);
        $body
    }
    if (faulty) {
        $flavor
    }
    return acc;
};

let s${k}_run = func(size: int32, faults: int32) -> int64 {
    let value = s${k}_arith(size * $m + $a, $fault);
    console.println("s$k $T {}", value);
    return $reduce;
};
""", k=k, T=name, init=p.between(1, 9), M=modulus, C=c, K=span, body=body, flavor=flavor, m=p.between(2, 6),
                                a=p.between(0, 9), fault=fault_test(bit), reduce=reduce))
    p.calls.append(k)


def section_loops(p, k):
    """Counted, conditional, post-test, and for-each loops with break and continue."""
    p.feature("loops")
    shape = p.between(0, 3)
    a, b, c = p.between(2, 6), p.between(3, 9), p.between(1, 5)
    if shape == 0:
        body = """let s${k}_loops = func(n: int32) -> int64 {
    let total: int64 = 0;
    let i: int32 = 0;
    while (i < n) {
        let j: int32 = 0;
        do {
            if ((i + j) % $a == $c) {
                j++;
                continue;
            }
            if (j > $b) {
                break;
            }
            total = (total * 3 + ((i * j) as int64)) & Mask;
            j++;
        } while (j < $b + 3);
        i++;
    }
    return total;
};
"""
    elif shape == 1:
        body = """let s${k}_loops = func(n: int32) -> int64 {
    let total: int64 = 1;
    let steps: int32 = 0;
    let x: int64 = (n as int64) + $b;
    while (x != 1 && steps < n * $a + 20) {
        x = x % 2 == 0 ? x / 2 : 3 * x + 1;
        total = (total * 5 + x) & Mask;
        steps += 1;
    }
    return mix(total, steps as int64);
};
"""
    elif shape == 2:
        body = """let s${k}_loops = func(n: int32) -> int64 {
    let weights: int32[] = [$a, $b, $c, 7];
    let total: int64 = 0;
    for (let i: int32 = 0; i < n * 2; i += $c) {
        for (let w in weights) {
            if (w == $b) {
                continue;
            }
            last term = (w * (i + 1)) as int64;
            total = (total + term) & Mask;
            if (total % 97 == $a) {
                break;
            }
        }
    }
    return total;
};
"""
    else:
        body = """let s${k}_loops = func(n: int32) -> int64 {
    let total: int64 = 0;
    let count: int32 = 0;
    for (let v in 0 .. n * $a : $c) {
        count += 1;
        total = (total * 7 + (v as int64) + (count as int64)) & Mask;
        if (v % $b == 0) {
            total += 1;
        }
    }
    return total;
};
"""
    p.declarations.append(fill(body, k=k, a=a, b=b, c=c) + fill("""
let s${k}_run = func(size: int32, faults: int32) -> int64 {
    let value = s${k}_loops(size * $m);
    console.println("s$k loops {}", value);
    return value;
};
""", k=k, m=p.between(2, 5)))
    p.calls.append(k)


def section_recursion(p, k):
    """Direct recursion and mutual recursion with a depth from the input; past the frame limit on the fault path."""
    p.feature("recursion", "mutual-recursion")
    bit = p.fault("deep-recursion")
    a, b = p.between(2, 7), p.between(3, 11)
    p.declarations.append(fill("""let s${k}_descend = func(depth: int32, acc: int64) -> int64 {
    if (depth <= 0) {
        return acc;
    }
    return s${k}_descend(depth - 1, mix(acc, (depth as int64) + $a));
};

let s${k}_tree = func(n: int32) -> int64 {
    if (n < 2) {
        return n as int64;
    }
    return s${k}_tree(n - 1) + s${k}_tree(n - 2) * $a;
};

let s${k}_ping = func(depth: int32, acc: int64) -> int64 {
    if (depth <= 0) {
        return acc;
    }
    return s${k}_pong(depth - 1, mix(acc, 1));
};

let s${k}_pong = func(depth: int32, acc: int64) -> int64 {
    if (depth <= 0) {
        return mix(acc, 2);
    }
    return s${k}_ping(depth - 1, mix(acc, (depth as int64) * $b));
};

let s${k}_run = func(size: int32, faults: int32) -> int64 {
    let depth = size % $d + $e;
    let down = s${k}_descend(depth, 0);
    let tree = s${k}_tree(size % 8 + $f);
    let pair = s${k}_ping(depth + 1, 0);
    console.println("s$k recursion {} {} {}", down, tree, pair);
    if ($fault) {
        console.println("s$k deep {}", s${k}_descend(1000000, 0));
    }
    return mix(mix(down, tree), pair);
};
""", k=k, a=a, b=b, d=p.between(7, 23), e=p.between(3, 9), f=p.between(2, 5), fault=fault_test(bit)))
    p.calls.append(k)


def section_classes(p, k):
    """Classes with fields, some read before they are written on some paths, inheritance, independent instances,
    and destroy followed by a use on the fault path."""
    p.feature("classes", "uninitialized-field", "destroy")
    bit_field = p.fault("uninitialized-field")
    bit_destroy = p.fault("use-after-destroy")
    a, b, c = p.between(2, 9), p.between(1, 7), p.between(3, 40)
    p.declarations.append(fill("""let Base$k = class (
    export let total: int64 = 0;
    export let count: int32 = 0;
    export let note: int64;
    export let label: string = "base$k";

    export let add = func(value: int64) -> void {
        total = (total * $a + value) & Mask;
        count += 1;
    };

    export let weigh = func() -> int64 {
        return total + (count as int64);
    };

    export let set_note = func(value: int64) -> void {
        note = value;
    };
);

let Derived$k = class:Base$k (
    export let extra: int64 = $c;

    export let.override weigh = func() -> int64 {
        return total * 2 + (count as int64) + extra;
    };
);

let s${k}_run = func(size: int32, faults: int32) -> int64 {
    let items: Base$k[list] = [];
    for (let i: int32 = 0; i < size % 5 + $b; i++) {
        if (i % 2 == 0) {
            items.append(new Base$k());
        } else {
            items.append(new Derived$k());
        }
    }
    let check: int64 = 0;
    for (let round: int32 = 0; round < size; round++) {
        for (let i: int32 = 0; i < items.length; i++) {
            items[i].add((round * 3 + i) as int64);
            if (i % 2 == 0) {
                items[i].set_note(items[i].weigh());
            }
        }
    }
    for (let i: int32 = 0; i < items.length; i++) {
        check = mix(check, items[i].weigh());
        if (i % 2 == 0 || $field) {
            check = mix(check, items[i].note);
        }
        console.println("s$k item {} {} {}", i, items[i].count, items[i].weigh());
    }
    let scratch = new Derived$k();
    scratch.add(check);
    scratch.label = "scratch";
    console.println("s$k {} {}", scratch.label, scratch.weigh());
    destroy scratch;
    if ($destroy) {
        console.println("s$k after destroy {}", scratch.label);
    }
    return check;
};
""", k=k, a=a, b=b, c=c, field=fault_test(bit_field), destroy=fault_test(bit_destroy)))
    p.calls.append(k)


def section_interfaces(p, k):
    """An interface with several implementations, called through the interface type."""
    p.feature("interfaces", "classes")
    count = p.between(2, 4)
    implementations = []
    names = ["Left", "Right", "Mid", "Edge"]
    for index in range(count):
        a, b = p.between(2, 9), p.between(1, 20)
        formulas = [
            f"(value * {a} + (bias as int64) + {b}) & Mask",
            f"(value + (bias as int64) * {a} + {b}) & Mask",
            f"(value ^ {b}) + (bias as int64) * {a}",
            f"value % {b + 3} + (bias as int64) + {a}",
        ]
        implementations.append(fill("""let $name$k = class implements Op$k (
    let bias: int32;
    export let init = func(start: int32) -> void {
        bias = start;
    };
    export let apply = func(value: int64) -> int64 {
        bias += 1;
        return $formula;
    };
    export let name = func() -> string => "$name$k";
);
""", name=names[index], k=k, formula=p.pick(formulas)))
    construct = "\n    ".join(f"ops.append(new {names[index]}{k}({p.between(0, 9)}));" for index in range(count))
    p.declarations.append(fill("""let Op$k = interface (
    let apply = func(value: int64) -> int64;
    let name = func() -> string;
    let describe = func() -> string {
        return "<" + name() + ">";
    };
);

""", k=k) + "\n".join(implementations) + fill("""
let s${k}_through = func(op: Op$k, value: int64) -> int64 => op.apply(value) + op.apply(value + 1);

let s${k}_run = func(size: int32, faults: int32) -> int64 {
    let ops: Op$k[list] = [];
    $construct
    let check: int64 = 0;
    for (let round: int32 = 0; round < size * $m; round++) {
        last op = ops[round % ops.length];
        check = mix(check, s${k}_through(op, check % 1000));
        if (round % $n == 0) {
            console.println("s$k {} {}", op.describe(), check);
        }
    }
    return check;
};
""", k=k, construct=construct, m=p.between(2, 4), n=p.between(3, 7)))
    p.calls.append(k)


def section_closures(p, k):
    """Closures returned from functions, composed, stored in an array, and called."""
    p.feature("closures")
    bit = p.fault("closure-division")
    a, b = p.between(2, 9), p.between(1, 30)
    p.declarations.append(fill("""let s${k}_make = func(offset: int64) -> func(int64) -> int64 {
    return func(value: int64) -> int64 {
        return (value * $a + offset) & Mask;
    };
};

let s${k}_compose = func(first: func(int64) -> int64, second: func(int64) -> int64) -> func(int64) -> int64 {
    return func(value: int64) -> int64 {
        return second(first(value));
    };
};

let s${k}_run = func(size: int32, faults: int32) -> int64 {
    let scale: int64 = (size as int64) + $b;
    let adders: (func(int64) -> int64)[] = [s${k}_make(1), s${k}_make(scale), s${k}_make($b)];
    let both = s${k}_compose(adders[0], adders[1]);
    let bump = func(value: int64) -> int64 {
        return value + scale;
    };
    let check: int64 = 0;
    for (let i: int32 = 0; i < size * $m; i++) {
        check = mix(check, both(adders[i % 3](bump(check) & 255)));
    }
    console.println("s$k closures {}", check);
    if ($fault) {
        let broken = func(value: int64) -> int64 {
            return 100 / (value - value);
        };
        check = mix(check, broken(check));
    }
    return check;
};
""", k=k, a=a, b=b, m=p.between(2, 6), fault=fault_test(bit)))
    p.calls.append(k)


def section_collections(p, k):
    """Arrays and lists of several element types with append, remove, nested arrays, and an index out of bounds
    on the fault path."""
    p.feature("arrays", "lists", "index-fault")
    bit = p.fault("index-out-of-bounds")
    a, b = p.between(3, 13), p.between(1, 9)
    kinds = [
        ("int32", "(i * $a + $b) % 97", "(items[i] as int64)"),
        ("int64", "((i as int64) * $a + $b) * 1000003", "(items[i] % 1009)"),
        ("uint8", "((i * $a + $b) % 251) as uint8", "(items[i] as int64)"),
        ("int16", "((i * $a + $b) % 300) as int16", "(items[i] as int64)"),
    ]
    element, make, read = p.pick(kinds)
    fault_text = p.pick([
        "check = mix(check, items[items.length] as int64);",
        "check = mix(check, items[0 - 1 - (size % 2)] as int64);",
        "items.remove_at(items.length);",
        "check = mix(check, grid[1][items.length + 5] as int64);",
    ])
    p.declarations.append(fill("""let s${k}_run = func(size: int32, faults: int32) -> int64 {
    let items: $E[list] = [];
    for (let i: int32 = 0; i < size * $m + 3; i++) {
        items.append($make);
    }
    let check: int64 = 0;
    for (let i: int32 = 0; i < items.length; i++) {
        check = mix(check, $read);
    }
    while (items.length > size) {
        items.remove_at(items.length / 2);
        if (items.length % 3 == 0) {
            check = mix(check, items[0] as int64);
        }
    }
    items.append(items[0]);
    let grid: int32[][] = [[1, 2, $b], [size, $a], [3]];
    let marks: bool[] = [true, false, true, false];
    for (let i: int32 = 0; i < items.length; i++) {
        let swap = items[i];
        items[i] = items[items.length - 1 - i];
        items[items.length - 1 - i] = swap;
        marks[i % 4] = !marks[i % 4];
    }
    let flags: int64 = 0;
    for (let flag in marks) {
        flags = flags * 2 + (flag ? 1 : 0);
    }
    let words: string[list] = [];
    for (let i: int32 = 0; i < size % 6 + 2; i++) {
        words.append("w" + i.to_string());
    }
    check = mix(check, (grid[0][2] + grid[1][1]) as int64);
    check = mix(check, flags);
    console.println("s$k collections {} {} {} {}", items.length, check, words.length, words[words.length - 1]);
    if ($fault) {
        $fault_text
    }
    return check;
};
""", k=k, E=element, make=make.replace("$a", str(a)).replace("$b", str(b)),
                                read=read, m=p.between(2, 5), a=a, b=b, fault=fault_test(bit),
                                fault_text=fault_text))
    p.calls.append(k)


def section_text(p, k):
    """Text built with a builder and by concatenation, split, trimmed, and sliced; a slice inside a character on
    the fault path."""
    p.feature("text", "slice")
    bit = p.fault("slice-boundary")
    words = ", ".join(f'"{w}"' for w in p.random.sample(WORDS, 4))
    sep = p.pick([",", ";", "|"])
    p.use("builders", "strings")
    p.declarations.append(fill("""let s${k}_run = func(size: int32, faults: int32) -> int64 {
    let words: string[] = [$words];
    let builder = builders.create();
    for (let i: int32 = 0; i < size + $a; i++) {
        if (i > 0) {
            builder.append("$sep");
        }
        if (i % 3 == 1) {
            builder.append(" ");
        }
        builder.append(words[i % 4]);
        builder.append_int((i * $b) as int64);
    }
    let text = builder.to_string();
    let parts = strings.split(text, "$sep");
    let check: int64 = 0;
    let rebuilt: string[list] = [];
    for (let part in parts) {
        last trimmed = strings.trim(part);
        rebuilt.append(trimmed);
        for (let i: int32 = 0; i < trimmed.byte_length; i++) {
            check = mix(check, trimmed.byte_at(i) as int64);
        }
    }
    let joined = strings.join(rebuilt, "/");
    let first = strings.index_of(joined, "/");
    let head = first > 0 ? joined.slice(0, first) : joined;
    let tail = joined.slice(joined.byte_length - 1, joined.byte_length);
    console.println("s$k text {} {} {} {}", parts.length, head, tail, joined.byte_length);
    let greeting = "été " + head;
    console.println("s$k {}", greeting.slice(0, 2) + greeting.slice(5, greeting.byte_length));
    if ($fault) {
        console.println("s$k {}", greeting.slice(0, 1));
    }
    return mix(check, (joined.byte_length as int64));
};
""", k=k, words=words, sep=sep, a=p.between(3, 8), b=p.between(2, 9), fault=fault_test(bit)))
    p.calls.append(k)


def section_kernel(p, k):
    """Standard library kernels: a checksum, a digest, a sort, and text hashing."""
    p.feature("kernel", "lists")
    p.use("crc32", "sha256", "sorting", "strings")
    a, b = p.between(3, 31), p.between(1, 17)
    p.declarations.append(fill("""let s${k}_run = func(size: int32, faults: int32) -> int64 {
    let data: uint8[list] = [];
    for (let i: int32 = 0; i < size * $m + 5; i++) {
        data.append(((i * $a + $b) % 251) as uint8);
    }
    let check: int64 = ((crc32.of(data) % 1000003) as int64);
    let digest = sha256.of(data);
    for (let byte in digest) {
        check = mix(check, byte as int64);
    }
    let keys: int64[list] = [];
    let seed: int64 = $a;
    for (let i: int32 = 0; i < size + 4; i++) {
        seed = (seed * 1103515245 + 12345) & 2147483647;
        keys.append(seed % 10007 - 5000);
    }
    sorting.sort_ascending(keys);
    for (let i: int32 = 1; i < keys.length; i++) {
        if (keys[i - 1] > keys[i]) {
            console.println("s$k unsorted");
        }
    }
    check = mix(check, keys[0] + 5000);
    check = mix(check, keys[keys.length - 1] + 5000);
    check = mix(check, (strings.hash("kernel $k" + size.to_string()) % 65521) as int64);
    console.println("s$k kernel {} {} {}", digest.length, keys[0], check);
    return check;
};
""", k=k, m=p.between(2, 9), a=a, b=b))
    p.calls.append(k)


def section_floats(p, k):
    """Float arithmetic, infinities, NaN-producing operations, and a NaN converted to an integer on the fault
    path."""
    p.feature("floats", "nan")
    p.use("math")
    bit = p.fault("nan-conversion")
    a, b = p.pick(["0.75", "0.5", "1.25", "0.125"]), p.pick(["0.125", "2.5", "0.0625"])
    p.declarations.append(fill("""let s${k}_fold = func(x: float64) -> int64 {
    if (x != x) {
        return -1;
    }
    if (x > 1000000000.0 || x < -1000000000.0) {
        return -2;
    }
    return (x * 1000.0) as int64;
};

let s${k}_run = func(size: int32, faults: int32) -> int64 {
    let x: float64 = $start;
    let y: float32 = 1.5;
    let nans: int32 = 0;
    let check: int64 = 0;
    for (let i: int32 = 0; i < size * $m; i++) {
        x = x * $a + (i as float64) * $b;
        y = y / 2.0 + 1.25;
        if (i % $n == 3) {
            x = x / 0.0;
        }
        if (i % $n == 4) {
            x = x - x;
        }
        if (x != x) {
            nans += 1;
            x = math.sqrt(-1.0) + 1.0;
            x = (i as float64);
        }
        check = mix(check, s${k}_fold(x) + s${k}_fold(y as float64));
    }
    let root = math.sqrt(x * x + 2.0);
    let logged = math.log(-1.0);
    console.println("s$k floats {} {} {} {} {}", x, y, root, logged, nans);
    check = mix(check, s${k}_fold(logged) + s${k}_fold(root) + (nans as int64));
    if (logged < 1.0 || logged >= 1.0 || logged == logged) {
        console.println("s$k ordered");
    }
    if ($fault) {
        check = mix(check, (logged as int64));
    }
    return check;
};
""", k=k, start=p.pick(["1.0", "0.5", "3.75"]), m=p.between(2, 6), a=a, b=b, n=p.between(5, 9),
                                fault=fault_test(bit)))
    p.calls.append(k)


def section_tasks(p, k):
    """A producer task and a consumer connected by a channel, and a fan-out of tasks joined in a scope; a task that
    traps on the fault path."""
    p.feature("tasks", "channels")
    p.tasks = True
    p.use("channels", "tasks")
    bit = p.fault("task-trap")
    a = p.between(2, 7)
    p.declarations.append(fill("""let s${k}_produce = func(out: channels.Sender<int32>, closer: channels.Closer<int32>, n: int32, broken: bool) -> int32 {
    for (let i: int32 = 0; i < n; i++) {
        out.send(i * $a + 1);
    }
    if (broken) {
        console.println("s$k producer trapping {}", n / (n - n));
    }
    closer.close();
    return n;
};

let s${k}_consume = func(incoming: channels.Receiver<int32>) -> int64 {
    let sum: int64 = 0;
    let open = true;
    while (open) {
        match (incoming.receive()) {
            channels.Received.Value(value) => {
                sum = (sum * 7 + (value as int64)) & Mask;
            },
            _ => {
                open = false;
            },
        }
    }
    return sum;
};

let s${k}_work = func(id: int32, n: int32) -> int64 {
    let total: int64 = 0;
    for (let i: int32 = 0; i < n; i++) {
        total = (total * 3 + (id as int64) + (i as int64)) & Mask;
    }
    return total;
};

let s${k}_run = func(size: int32, faults: int32) -> int64 {
    let [out, incoming, closer] = channels.open<int32>($cap);
    let total: int64 = 0;
    scope {
        let producer = spawn s${k}_produce(out, closer, size + $b, $fault);
        total = s${k}_consume(incoming);
        total = mix(total, producer.join() as int64);
    }
    let fan: int64 = 0;
    scope {
        let pending: tasks.Task<int64>[list] = [];
        for (let id: int32 = 0; id < size % 4 + 2; id++) {
            pending.append(spawn s${k}_work(id, size + id));
        }
        for (let task: tasks.Task<int64> in pending) {
            fan = mix(fan, task.join());
        }
    }
    console.println("s$k tasks {} {}", total, fan);
    return mix(total, fan);
};
""", k=k, a=a, b=p.between(2, 9), cap=p.pick([0, 1, 2, 3]), fault=fault_test(bit)))
    p.calls.append(k)


SECTIONS = [
    (section_integers, 100),
    (section_loops, 80),
    (section_recursion, 70),
    (section_classes, 75),
    (section_interfaces, 65),
    (section_closures, 65),
    (section_collections, 80),
    (section_text, 65),
    (section_kernel, 55),
    (section_floats, 65),
    (section_tasks, 30),
]


def build_program(seed):
    """The program for a seed: its source text and the argument list to run it with."""
    p = Program(seed)
    p.use("console", "program", "int32s", "result")
    chosen = [make for make, percent in SECTIONS if p.chance(percent)]
    while len(chosen) < 5:
        extra = p.pick(SECTIONS)[0]
        if extra not in chosen:
            chosen.append(extra)
    # Source order is declaration order, so the sections go in a fixed order.
    chosen.sort(key=lambda make: [m for m, _ in SECTIONS].index(make))
    if p.chance(35):
        # A second integer section, at another width.
        chosen.append(section_integers)
    for make in chosen:
        k = p.next_id
        p.next_id += 1
        make(p, k)
    size = p.between(3, 14)
    faults = 0
    if p.fault_bits and p.chance(45):
        faults = 1 << p.between(0, len(p.fault_bits) - 1)
        if p.chance(15) and len(p.fault_bits) > 1:
            faults |= 1 << p.between(0, len(p.fault_bits) - 1)
        p.feature("fault-path")
    imports = "\n".join(f"let {alias} = import:{IMPORTS[alias]};" for alias in p.imports)
    calls = "\n".join(
        f"        check = mix(check, s{k}_run(size, faults));" for k in p.calls)
    header = f"""// Generated by scripts/program-fuzz.py from seed {seed}; the program is a pure function of the seed.
// args: {size} {faults}
// tasks: {'yes' if p.tasks else 'no'}
// features: {' '.join(p.features)}
// faults: {' '.join(p.fault_bits)}
"""
    source = header + imports + """

const Mask: int64 = 1073741823;

let mix = func(check: int64, value: int64) -> int64 => (check * 31 + value) & Mask;

let on = func(faults: int32, bit: int32) -> bool => ((faults >> bit) & 1) == 1;

""" + "\n".join(p.declarations) + f"""
export let Fuzz = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {{
        last size = result.value_or<int32>(int32s.parse(args[0]), 3);
        last faults = result.value_or<int32>(int32s.parse(args[1]), 0);
        let check: int64 = size as int64;
        console.println("size {{}}", size);
{calls}
        console.println("checksum {{}}", check);
        return program.ExitCode.Ok;
    }};
);
"""
    return source, [str(size), str(faults)], p


def generate(args):
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for seed in parse_seeds(args.seed):
        source, _, _ = build_program(seed)
        (out / f"program-{seed}.nra").write_text(source)
    return 0


def parse_seeds(text):
    seeds = []
    for part in str(text).split(","):
        if ".." in part:
            low, high = part.split("..")
            seeds.extend(range(int(low), int(high) + 1))
        else:
            seeds.append(int(part))
    return seeds


def coverage(args):
    """The share of programs that use each feature."""
    seeds = parse_seeds(args.seeds)
    counts = {name: 0 for name in FEATURES}
    for seed in seeds:
        for name in build_program(seed)[2].features:
            counts[name] += 1
    for name in FEATURES:
        print(f"{name:20} {counts[name]:5} of {len(seeds)}  {100.0 * counts[name] / len(seeds):5.1f}%")
    return 0


# --- the differential runner ----------------------------------------------------------------------------------------

# Environment variables the tier machinery reads: cleared for every run so that the modes below are the only ones.
TIER_VARIABLES = ["NEURA_TIER", "NEURA_TIER_EAGER", "NEURA_TIER_CHECK", "NEURA_TIER_PROGRAMS", "NEURA_TIER_CHECK_HEAP",
                  "NEURA_TIER_DUMP", "NEURA_JIT", "NEURA_RUN_OPTIONS", "NEURA_TIER3", "NEURA_TIER3_VERIFY"]

REFERENCE_OPTIONS = ["--tier=0"]

# (name, run options, environment)
MODES = [
    ("jit-off", ["--jit=off"], {}),
    ("jit-on", ["--jit=on"], {}),
    ("eager-jit-off", ["--jit=off"], {"NEURA_TIER_EAGER": "1"}),
    ("eager-jit-on", ["--jit=on"], {"NEURA_TIER_EAGER": "1"}),
    ("programs-jit-off", ["--jit=off"], {"NEURA_TIER_PROGRAMS": "1"}),
    ("programs-jit-on", ["--jit=on"], {"NEURA_TIER_PROGRAMS": "1"}),
    ("eager-programs-jit-off", ["--jit=off"], {"NEURA_TIER_EAGER": "1", "NEURA_TIER_PROGRAMS": "1"}),
    ("eager-programs-jit-on", ["--jit=on"], {"NEURA_TIER_EAGER": "1", "NEURA_TIER_PROGRAMS": "1"}),
    ("eager-check2-programs", ["--jit=on"],
     {"NEURA_TIER_EAGER": "1", "NEURA_TIER_CHECK": "2", "NEURA_TIER_PROGRAMS": "1"}),
    ("tier3-interp", ["--jit=on"], {"NEURA_TIER3": "interp", "NEURA_TIER_EAGER": "1", "NEURA_TIER3_VERIFY": "1"}),
    ("tier3-interp-check2", ["--jit=on"],
     {"NEURA_TIER3": "interp", "NEURA_TIER_EAGER": "1", "NEURA_TIER3_VERIFY": "1", "NEURA_TIER_CHECK": "2"}),
]

QUICK_SEEDS = [1, 2, 5]


class Outcome:
    """What one run did: exit status (None for a timeout), standard output, and standard error."""

    def __init__(self, status, stdout, stderr):
        self.status = status
        self.stdout = stdout
        self.stderr = stderr

    def key(self):
        return (self.status, self.stdout, self.stderr)

    def text(self):
        status = "timed out" if self.status is None else str(self.status)
        return (f"exit status: {status}\n--- stdout ---\n{self.stdout.decode(errors='replace')}"
                f"--- stderr ---\n{self.stderr.decode(errors='replace')}")


class Runner:
    """Runs one image with options and an environment; counts its runs."""

    def __init__(self, neura, timeout):
        self.neura = neura
        self.timeout = timeout
        self.runs = 0

    def run(self, image, arguments, options, environment):
        env = {key: value for key, value in os.environ.items() if key not in TIER_VARIABLES}
        # No graph device: a run on one skips tier 2, and a machine with a GPU would then compare different
        # things from one without. A mode may override this.
        env["NEURA_GPU"] = "off"
        env.update(environment)
        self.runs += 1
        command = [self.neura, "run", "--stats", "--profile", *options, str(image), "--", *arguments]
        try:
            result = subprocess.run(command, capture_output=True, env=env, stdin=subprocess.DEVNULL,
                                    timeout=self.timeout)
        except subprocess.TimeoutExpired as expired:
            return Outcome(None, expired.stdout or b"", expired.stderr or b"")
        return Outcome(result.returncode, result.stdout, result.stderr)


STATS = re.compile(rb"stats: steps (\d+), peak heap cells (\d+)")
HEAP_ENTRY = re.compile(rb"heap limit exceeded: allocation of (\d+) cells before entry")


def spread(low, high, count):
    """`count` integers from low to high, both ends included, evenly spaced."""
    if high - low + 1 <= count:
        return list(range(low, high + 1))
    return sorted({low + (high - low) * i // (count - 1) for i in range(count)})


def step_points(steps, coarse):
    """The step limits of a sweep. A full sweep (`coarse` 1) takes every value when the run has at most 2000 steps,
    and otherwise the first 200, the last 200, and 100 values spread over the rest; a coarse sweep divides those
    counts by `coarse`."""
    if steps <= 2000 and coarse == 1:
        return list(range(1, steps + 1))
    edge = 200 // coarse
    points = set(range(1, min(steps, edge) + 1)) | set(range(max(1, steps - edge + 1), steps + 1))
    if steps > 2 * edge:
        points |= set(spread(edge + 1, steps - edge, max(2, 100 // coarse)))
    return sorted(points)


def limit_options(kind, value):
    if kind == "steps":
        return ["--max-steps", str(value)]
    if kind == "heap":
        return ["--max-heap-cells", str(value)]
    if kind == "frames":
        return ["--max-frames", str(value)]
    if kind == "quantum":
        return ["--task-quantum", str(value)]
    return []


class Failure:
    def __init__(self, seed, mode, kind, limit, reference, actual, description):
        self.seed = seed
        self.mode = mode
        self.kind = kind
        self.limit = limit
        self.reference = reference
        self.actual = actual
        self.description = description


class SeedResult:
    def __init__(self, seed):
        self.seed = seed
        self.failures = []
        self.points = 0
        self.runs = 0
        self.steps = 0
        self.seconds = 0.0


def check_seed(seed, coarse, runner, work, modes):
    """Builds a seed's program, runs it at tier 0 and in every mode, and sweeps the limits. Each mode's first
    difference in each kind of run (no limit, steps, heap, frames, quantum) is a failure."""
    result = SeedResult(seed)
    started = time.monotonic()
    source, arguments, program = build_program(seed)
    program_path = work / f"program-{seed}.nra"
    image = work / f"program-{seed}.nvr"
    program_path.write_text(source)
    build = subprocess.run([runner.neura, "build", str(program_path), "-o", str(image)], capture_output=True,
                           stdin=subprocess.DEVNULL)
    if build.returncode != 0 or build.stderr or build.stdout:
        result.failures.append(Failure(seed, "build", "build", None, Outcome(0, b"", b""),
                                       Outcome(build.returncode, build.stdout, build.stderr),
                                       "the generated program does not compile without diagnostics"))
        result.seconds = time.monotonic() - started
        return result
    reported = set()

    def compare(kind, value, extra):
        """Runs the reference and every mode under one set of limits."""
        result.points += 1
        reference = runner.run(image, arguments, REFERENCE_OPTIONS + extra, {})
        if reference.status is None:
            if ("reference", kind) not in reported:
                reported.add(("reference", kind))
                result.failures.append(Failure(seed, "reference", kind, value, reference, reference,
                                               "the tier 0 reference run exceeded the time limit"))
            return reference
        for name, mode_options, environment in modes:
            if (name, kind) in reported:
                continue
            actual = runner.run(image, arguments, mode_options + extra, environment)
            if actual.key() != reference.key():
                reported.add((name, kind))
                what = "exceeded the time limit" if actual.status is None else "differs from tier 0"
                result.failures.append(Failure(seed, name, kind, value, reference, actual, what))
        return reference

    base = compare("base", None, [])
    stats = STATS.search(base.stderr)
    if not stats:
        result.failures.append(Failure(seed, "reference", "base", None, base, base, "no --stats line"))
        result.seconds = time.monotonic() - started
        return result
    steps, peak = int(stats.group(1)), int(stats.group(2))
    result.steps = steps
    if not coarse:
        result.runs = runner.runs
        result.seconds = time.monotonic() - started
        return result

    for value in step_points(steps, coarse):
        compare("steps", value, limit_options("steps", value))

    # The smallest heap limit that admits the first allocation, then up to the peak and a little past it.
    low = 1
    for _ in range(40):
        probe = runner.run(image, arguments, REFERENCE_OPTIONS + ["--max-heap-cells", str(low)], {})
        entry = HEAP_ENTRY.search(probe.stderr)
        if not entry or int(entry.group(1)) <= low:
            break
        low = int(entry.group(1))
    high = peak + 2
    if high - low + 1 > 500 or coarse > 1:
        heap_points = spread(low, high, 200 // coarse)
    else:
        heap_points = list(range(low, high + 1))
    for value in heap_points:
        compare("heap", value, limit_options("heap", value))

    # The deepest call depth: the smallest frame limit under which the run does not trap on the limit.
    def trapped(limit):
        probe = runner.run(image, arguments, REFERENCE_OPTIONS + ["--max-frames", str(limit)], {})
        return b"frame depth limit exceeded" in probe.stderr

    depth = 1
    while trapped(depth) and depth < (1 << 24):
        depth *= 2
    if depth > 1:
        below, above = depth // 2, depth
        while above - below > 1:
            middle = (below + above) // 2
            if trapped(middle):
                below = middle
            else:
                above = middle
        depth = above
    top = depth + 2
    for value in range(1, top + 1) if top <= 500 and coarse == 1 else spread(1, top, 200 if coarse == 1 else 10):
        compare("frames", value, limit_options("frames", value))

    if program.tasks:
        for value in range(1, 5 if coarse == 1 else 3):
            compare("quantum", value, limit_options("quantum", value))
    result.runs = runner.runs
    result.seconds = time.monotonic() - started
    return result


def write_failure(report, failure, neura, script):
    """Writes a difference: the program, both outputs, and a shell script that reproduces it."""
    name = f"seed-{failure.seed}-{failure.mode}-{failure.kind}" + (
        f"-{failure.limit}" if failure.limit is not None else "")
    directory = report / name
    directory.mkdir(parents=True, exist_ok=True)
    source, arguments, _ = build_program(failure.seed)
    (directory / f"program-{failure.seed}.nra").write_text(source)
    (directory / "reference.txt").write_text(failure.reference.text())
    (directory / "actual.txt").write_text(failure.actual.text())
    extra = " ".join(limit_options(failure.kind, failure.limit))
    options, environment = {mode[0]: (mode[1], mode[2]) for mode in MODES}.get(failure.mode, ([], {}))
    given = " ".join(shlex.quote(argument) for argument in arguments)
    exports = " ".join(f"{key}={value}" for key, value in environment.items())
    lines = [
        "#!/bin/sh",
        f"# {failure.description}: seed {failure.seed}, mode {failure.mode}, {failure.kind} limit {failure.limit}",
        "# Prints the differences between the tier 0 run and the mode's run, and exits 1 when there are any.",
        "set -u",
        f"NEURA={shlex.quote(str(neura))}",
        "WORK=$(mktemp -d)",
        f"python3 {shlex.quote(str(script))} generate --seed {failure.seed} --out \"$WORK\"",
        f"\"$NEURA\" build \"$WORK/program-{failure.seed}.nra\" -o \"$WORK/program.nvr\" || exit 2",
    ]
    if failure.mode != "build":
        reference = f"\"$NEURA\" run --stats --profile {' '.join(REFERENCE_OPTIONS)} {extra} \"$WORK/program.nvr\" -- {given}"
        actual = f"env {exports} \"$NEURA\" run --stats --profile {' '.join(options)} {extra} \"$WORK/program.nvr\" -- {given}"
        lines += [
            f"{reference} > \"$WORK/reference.out\" 2> \"$WORK/reference.err\"; echo $? > \"$WORK/reference.status\"",
            f"{actual} > \"$WORK/actual.out\" 2> \"$WORK/actual.err\"; echo $? > \"$WORK/actual.status\"",
            "cd \"$WORK\"",
            "different=0",
            "for part in status out err; do diff reference.$part actual.$part || different=1; done",
            "exit $different",
        ]
    (directory / "reproduce.sh").write_text("\n".join(lines) + "\n")
    os.chmod(directory / "reproduce.sh", 0o755)
    return directory


PROBE = """let program = import:Std.Program;

let Probe = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        return program.ExitCode.Ok;
    };
);
"""


def can_run_native_code(neura):
    """Whether this binary can run regions as native code here (`--jit=on` does not fail)."""
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "probe.nra"
        source.write_text(PROBE)
        image = Path(directory) / "probe.nvr"
        built = subprocess.run([neura, "build", str(source), "-o", str(image)], capture_output=True)
        if built.returncode != 0:
            sys.exit("cannot build the probe program:\n" + built.stderr.decode(errors="replace"))
        return subprocess.run([neura, "run", "--jit=on", str(image)], capture_output=True).returncode == 0


def run_command(args):
    neura = str(Path(args.neura).resolve())
    seeds = parse_seeds(args.seeds) if args.seeds else QUICK_SEEDS
    coarse = args.coarse if args.coarse else (40 if args.quick else 1)
    if args.no_sweeps:
        coarse = 0
    modes = MODES
    if args.modes:
        wanted = args.modes.split(",")
        names = [mode[0] for mode in MODES]
        unknown = [name for name in wanted if name not in names]
        if unknown:
            sys.exit("unknown modes: " + ", ".join(unknown) + "; the modes are " + ", ".join(names))
        modes = [mode for mode in MODES if mode[0] in wanted]
    if any("--jit=on" in mode[1] for mode in modes) and not can_run_native_code(neura):
        print("this binary cannot run native code here (--jit=on fails): the jit-on modes are skipped")
        modes = [mode for mode in modes if "--jit=on" not in mode[1]]
    report = Path(args.report).resolve() if args.report else None
    failures = []
    started = time.monotonic()
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)

        def one(seed):
            return check_seed(seed, coarse, Runner(neura, args.timeout), work, modes)

        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = [pool.submit(one, seed) for seed in seeds]
            for result in (future.result() for future in futures):
                verdict = "ok" if not result.failures else f"{len(result.failures)} DIFFERENCES"
                print(f"seed {result.seed}: {verdict} ({result.steps} steps, {result.points} limit points, "
                      f"{result.runs} runs, {result.seconds:.1f}s)", flush=True)
                for failure in result.failures:
                    where = f"{failure.mode} {failure.kind}" + (
                        f" {failure.limit}" if failure.limit is not None else "")
                    print(f"  {failure.description}: {where}", flush=True)
                    if report is not None:
                        report.mkdir(parents=True, exist_ok=True)
                        folder = write_failure(report, failure, neura, Path(__file__).resolve())
                        print(f"    written to {folder}", flush=True)
                failures.extend(result.failures)
    print(f"{len(seeds)} seeds, {len(modes)} modes, {len(failures)} differences, "
          f"{time.monotonic() - started:.1f}s")
    return 1 if failures else 0


def compile_command(args):
    """Checks that every seed in a range compiles without a diagnostic."""
    neura = str(Path(args.neura).resolve())
    seeds = parse_seeds(args.seeds)
    bad = []
    with tempfile.TemporaryDirectory() as directory:
        def one(seed):
            path = Path(directory) / f"program-{seed}.nra"
            path.write_text(build_program(seed)[0])
            return seed, subprocess.run([neura, "check", str(path)], capture_output=True, stdin=subprocess.DEVNULL)

        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for seed, checked in pool.map(one, seeds):
                if checked.returncode != 0 or checked.stdout or checked.stderr:
                    bad.append(seed)
                    print(f"seed {seed} does not compile cleanly:\n{(checked.stdout + checked.stderr).decode()}")
    good = len(seeds) - len(bad)
    print(f"{good} of {len(seeds)} seeds compile without diagnostics ({100.0 * good / len(seeds):.1f}%)")
    return 1 if bad else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    generate_parser = commands.add_parser("generate", help="write the program of a seed")
    generate_parser.add_argument("--seed", required=True, help="a seed, a list a,b, or a range a..b")
    generate_parser.add_argument("--out", required=True)
    run_parser = commands.add_parser("run", help="compare every mode against tier 0 under limit sweeps")
    run_parser.add_argument("--neura", required=True)
    run_parser.add_argument("--seeds", help="a seed, a list a,b, or a range a..b (default with --quick: 1,2,5)")
    run_parser.add_argument("--modes", help="a comma-separated subset of: " + ", ".join(mode[0] for mode in MODES))
    run_parser.add_argument("--jobs", type=int, default=2, help="seeds checked at once")
    run_parser.add_argument("--report", help="a directory for the differences")
    run_parser.add_argument("--timeout", type=float, default=120.0, help="seconds a single run may take")
    run_parser.add_argument("--coarse", type=int, help="divide the number of points of every sweep by this "
                                                       "(1 is the full sweep; --quick is 40)")
    run_parser.add_argument("--no-sweeps", action="store_true", help="only the runs without a limit")
    run_parser.add_argument("--quick", action="store_true", help="a few seeds under coarse sweeps")
    compile_parser = commands.add_parser("compile", help="check that every seed compiles without diagnostics")
    compile_parser.add_argument("--neura", required=True)
    compile_parser.add_argument("--seeds", required=True)
    compile_parser.add_argument("--jobs", type=int, default=2)
    coverage_parser = commands.add_parser("coverage", help="the share of programs that use each feature")
    coverage_parser.add_argument("--seeds", required=True)
    args = parser.parse_args()
    if args.command == "generate":
        return generate(args)
    if args.command == "run":
        return run_command(args)
    if args.command == "compile":
        return compile_command(args)
    return coverage(args)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Writes tests/stdlib/floats-exhaustive/<type>-round-trip-tests.nra.

Every bit pattern is checked in Neura: to_string then parse gives the same
value (NaN gives NaN), and the digest of the to_string texts of each chunk
equals the digest of the Fraction reference texts computed here.
"""
import sys
import os
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import floatref as fr  # noqa: E402
CHUNKS = 16
MASK = 0xFFFFFFFF


def string_hash(s):
    h = 0
    for byte in s.encode():
        h = (31 * h + byte) & MASK
    return h


for name, module, fname in (("float16", "Float16", "float16"), ("bfloat16", "BFloat16", "b-float16")):
    fmt = fr.Fmt(name)
    size = 65536 // CHUNKS
    digests = []
    for c in range(CHUNKS):
        d = 0
        for bits in range(c * size, (c + 1) * size):
            d = (31 * d + string_hash(fr.to_string(fmt, bits))) & MASK
        digests.append(d)
    tests = []
    for c in range(CHUNKS):
        tests.append(f"""
    export let test_patterns_{c * size}_to_{(c + 1) * size - 1} = func() -> void {{
        check_range({c * size}, {(c + 1) * size}, {digests[c]});
    }};
""")
    text = f"""let floats = import:Std.Core.{module};
let hashing = import:Std.Core.Hash;
let result = import:Std.Core.Result;
let test = import:Std.Test;

// Every {name} bit pattern (Stdlib.md sections 7.5 and 17): to_string then
// parse returns the same value (every NaN parses to NaN), and the digest of
// each chunk's to_string texts equals the digest of texts from an exact
// Fraction reference (generated with Python; digest = 31 * digest + the
// text's hash, wrapping, as Hash.combine).
let {module}RoundTripTests = module (
    let check_range = func(start: int, end: int, expected_digest: uint32) -> void {{
        let digest: uint32 = 0;
        for (let i = start; i < end; i = i + 1) {{
            let v = floats.from_bits(i as uint16);
            let text = floats.to_string(v);
            digest = hashing.combine(digest, text.hash_code());
            match (floats.parse(text)) {{
                result.Result.Ok(back) => {{
                    if (!floats.equals(back, v)) {{
                        test.fail("pattern " + i.to_string() + " (" + text + ") parsed to " + floats.to_string(back));
                    }}
                }},
                result.Result.Err(failure) => {{
                    test.fail("pattern " + i.to_string() + " (" + text + ") did not parse");
                }},
            }}
        }}
        test.assert_with(digest == expected_digest, "to_string digest differs from the reference");
    }};
{''.join(tests)});
"""
    path = f"{ROOT}/tests/stdlib/floats-exhaustive/{fname}-round-trip-tests.nra"
    with open(path, "w") as out:
        out.write(text)
    print(path, digests[:3])

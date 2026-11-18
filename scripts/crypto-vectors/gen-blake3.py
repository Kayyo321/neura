#!/usr/bin/env python3
"""Known answers for Std.Crypto.Hash.Blake3: the official BLAKE3 test vectors.

Copies the cases of test_vectors/test_vectors.json from the BLAKE3 reference
repository at a pinned commit, restricted to inputs of at most MAX_INPUT bytes
(the NVM interprets the compression function, so the 102400-byte cases are
left out; every case up to 4097 bytes, which crosses one, two, three and four
chunk boundaries, is kept). Each case keeps all 131 bytes of the extended
output of hash, keyed_hash and derive_key. The input of a case is the
repeating sequence 0, 1, ..., 250 of the given length; the test rebuilds it.

    python3 scripts/crypto-vectors/gen-blake3.py
"""

import json
import sys
import urllib.request

from nra_vectors import write_module

COMMIT = "657284b1fa4c4378aa7422b4450dcb5c2c9052fc"
URL = f"https://raw.githubusercontent.com/BLAKE3-team/BLAKE3/{COMMIT}/test_vectors/test_vectors.json"
MAX_INPUT = 4097


def main() -> int:
    with urllib.request.urlopen(URL, timeout=60) as response:
        data = json.load(response)
    rows = [[str(case["input_len"]), case["hash"], case["keyed_hash"], case["derive_key"]]
            for case in data["cases"] if case["input_len"] <= MAX_INPUT]
    print(write_module(
        "crypto-hash", "blake3-vectors.nra", "Blake3Vectors",
        ["python3 scripts/crypto-vectors/gen-blake3.py: the official vectors,", URL,
         f"(cases with input_len <= {MAX_INPUT}). key: {data['key']}",
         f"context_string: {data['context_string']}"],
        [("parameters", ["keyed_hash key (ASCII)", "derive_key context"], [[data["key"], data["context_string"]]]),
         ("cases", ["input length", "hash", "keyed_hash", "derive_key (131 bytes each)"], rows)]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

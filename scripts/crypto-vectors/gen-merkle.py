#!/usr/bin/env python3
"""Expected values for Std.Crypto.Hash.MerkleTree from an independent Python reference.

No standard publishes test vectors for these functions, so this script
implements the definitions of RFC 6962, section 2.1 (MTH, PATH and PROOF/
SUBPROOF, the same tree that RFC 9162, section 2.1 specifies) directly as
written there, over hashlib's SHA-256 and SHA3-256, and the Neura tests
compare against it. Leaf i is nra_vectors.pattern(i, 7, i) (leaf 0 is empty).

    python3 scripts/crypto-vectors/gen-merkle.py
"""

import hashlib
import sys

from nra_vectors import pattern, write_module

SIZES = 13


def leaf(data, h):
    return h(b"\x00" + data).digest()


def mth(leaves, h):
    n = len(leaves)
    if n == 0:
        return h(b"").digest()
    if n == 1:
        return leaf(leaves[0], h)
    k = 1
    while k * 2 < n:
        k *= 2
    return h(b"\x01" + mth(leaves[:k], h) + mth(leaves[k:], h)).digest()


def path(m, leaves, h):
    n = len(leaves)
    if n == 1:
        return []
    k = 1
    while k * 2 < n:
        k *= 2
    if m < k:
        return path(m, leaves[:k], h) + [mth(leaves[k:], h)]
    return path(m - k, leaves[k:], h) + [mth(leaves[:k], h)]


def subproof(m, leaves, b, h):
    n = len(leaves)
    if m == n:
        return [] if b else [mth(leaves, h)]
    k = 1
    while k * 2 < n:
        k *= 2
    if m <= k:
        return subproof(m, leaves[:k], b, h) + [mth(leaves[k:], h)]
    return subproof(m - k, leaves[k:], False, h) + [mth(leaves[:k], h)]


def main() -> int:
    data = [pattern(i, 7, i) for i in range(SIZES)]
    roots, inclusions, consistencies = [], [], []
    for name, h in [("sha256", hashlib.sha256), ("sha3_256", hashlib.sha3_256)]:
        for n in range(SIZES + 1):
            roots.append([name, str(n), mth(data[:n], h).hex()])
            for m in range(n):
                inclusions.append([name, str(m), str(n), "".join(x.hex() for x in path(m, data[:n], h))])
            for m in range(1, n):
                consistencies.append([name, str(m), str(n),
                                      "".join(x.hex() for x in subproof(m, data[:n], True, h))])
    print(write_module(
        "crypto-hash", "merkle-vectors.nra", "MerkleVectors",
        ["python3 scripts/crypto-vectors/gen-merkle.py: an independent Python reference of RFC 6962 section 2.1",
         "(no standard vectors exist). Leaf i is pattern(i, 7, i); proofs are concatenated hashes."],
        [("roots", ["hash", "tree size", "root"], roots),
         ("inclusion", ["hash", "leaf index", "tree size", "audit path"], inclusions),
         ("consistency", ["hash", "old size", "new size", "consistency proof"], consistencies)]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

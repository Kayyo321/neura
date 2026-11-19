#!/usr/bin/env python3
"""Known answers for Shamir secret sharing (Std.Crypto.Advanced.Sss).

GF(2^8): the reference is HashiCorp Vault's shamir package, shamir/shamir.go at
commit 7fd1fb52ece7b70cd663f2c0eaff6fccde296f0d (downloaded to
/home/dev/neura-gate/030/crypto2/sources/advanced/vault-shamir/, recorded in
SOURCES.txt there). It works byte by byte over GF(2^8) with the AES polynomial
x^8 + x^4 + x^3 + x + 1 (0x11B), and a share is the y bytes followed by the x
byte. The generator copies the file unchanged into a temporary Go module, adds
one file in the same package that builds a `polynomial` from given
coefficients (Vault's `makePolynomial` draws them from crypto/rand, which a
reproducible run cannot use) and evaluates it with Vault's own `evaluate`, and
checks every share set with Vault's exported `Combine`. Neura's split uses the
x-coordinates 1..n and takes the coefficient bytes in Vault's order (for each
secret byte, a_1 .. a_(t-1)); one case uses scattered x-coordinates, as Vault's
random permutation gives, for combination only.

Prime field: the prime is 2^521 - 1. A coefficient is 82 random bytes,
big-endian, reduced modulo p; shares are (x, f(x)) for x = 1..n. The
reference is the plain Python below (Horner evaluation and Lagrange
interpolation at 0), independent of the Neura code.

Random bytes are nra_vectors.pattern bytes.

    python3 scripts/crypto-vectors/gen-advanced-sss.py
"""

import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

from nra_vectors import pattern, write_module

VAULT = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/advanced/vault-shamir/shamir.go")
VAULT_COMMIT = "7fd1fb52ece7b70cd663f2c0eaff6fccde296f0d"
PRIME = 2 ** 521 - 1

EXPORT_GO = """package shamir

// Added by gen-advanced-sss.py: Vault's polynomial and evaluate with given coefficients.
func EvaluateWith(coefficients []uint8, x uint8) uint8 {
	p := polynomial{coefficients: coefficients}
	return p.evaluate(x)
}
"""

MAIN_GO = """package main

import (
	"encoding/hex"
	"encoding/json"
	"os"

	"gen/shamir"
)

type Case struct {
	Secret    string `json:"secret"`
	Threshold int    `json:"threshold"`
	Xs        []int  `json:"xs"`
	Random    string `json:"random"`
	Subsets   [][]int `json:"subsets"`
}

func main() {
	var cases []Case
	if err := json.NewDecoder(os.Stdin).Decode(&cases); err != nil {
		panic(err)
	}
	var out [][]string
	for _, c := range cases {
		secret, _ := hex.DecodeString(c.Secret)
		random, _ := hex.DecodeString(c.Random)
		t := c.Threshold
		shares := make([][]byte, len(c.Xs))
		for i, x := range c.Xs {
			shares[i] = make([]byte, len(secret)+1)
			shares[i][len(secret)] = uint8(x)
		}
		for idx, val := range secret {
			coefficients := append([]uint8{val}, random[idx*(t-1):(idx+1)*(t-1)]...)
			for i, x := range c.Xs {
				shares[i][idx] = shamir.EvaluateWith(coefficients, uint8(x))
			}
		}
		for _, subset := range c.Subsets {
			var parts [][]byte
			for _, i := range subset {
				parts = append(parts, shares[i])
			}
			got, err := shamir.Combine(parts)
			if err != nil || hex.EncodeToString(got) != c.Secret {
				panic("Vault Combine does not recover the secret")
			}
		}
		var row []string
		for _, s := range shares {
			row = append(row, hex.EncodeToString(s))
		}
		out = append(out, row)
	}
	json.NewEncoder(os.Stdout).Encode(out)
}
"""


def run_vault(cases):
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "shamir").mkdir()
        shutil.copy(VAULT, root / "shamir" / "shamir.go")
        (root / "shamir" / "export.go").write_text(EXPORT_GO)
        (root / "main.go").write_text(MAIN_GO)
        (root / "go.mod").write_text("module gen\n\ngo 1.21\n")
        done = subprocess.run(["go", "run", "."], cwd=root, input=json.dumps(cases), capture_output=True,
                              text=True, check=True, env={"PATH": "/usr/local/go/bin:/usr/bin:/bin",
                                                          "HOME": str(pathlib.Path.home()),
                                                          "GOFLAGS": "-mod=mod", "GOTOOLCHAIN": "local"})
        return json.loads(done.stdout)


def gf_cases():
    # (secret length, threshold, xs, subsets of share indexes to combine)
    specs = [
        (1, 2, [1, 2], [[0, 1], [1, 0]]),
        (16, 3, [1, 2, 3, 4, 5], [[0, 1, 2], [2, 3, 4], [4, 0, 2], [0, 1, 2, 3, 4]]),
        (32, 7, list(range(1, 11)), [list(range(7)), list(range(3, 10)), list(range(10))]),
        (33, 2, list(range(1, 256)), [[0, 254], [100, 7], list(range(255))]),
        (2, 255, list(range(1, 256)), [list(range(255))]),
        (5, 4, [17, 203, 99, 1, 255, 42], [[0, 1, 2, 3], [5, 4, 3, 2], [0, 1, 2, 3, 4, 5]]),
    ]
    cases = []
    for number, (length, threshold, xs, subsets) in enumerate(specs):
        cases.append({"secret": pattern(length, 37, number + 1).hex(), "threshold": threshold, "xs": xs,
                      "random": pattern(length * (threshold - 1), 13, 5 + number).hex(), "subsets": subsets})
    return cases


def prime_cases():
    rows = []
    for number, (length, parts, threshold) in enumerate([(1, 2, 2), (32, 5, 3), (65, 6, 6), (16, 9, 4)]):
        secret = pattern(length, 41, number + 3)
        random = pattern(82 * (threshold - 1), 19, number + 11)
        coefficients = [int.from_bytes(secret, "big")]
        for i in range(threshold - 1):
            coefficients.append(int.from_bytes(random[82 * i:82 * (i + 1)], "big") % PRIME)
        shares = []
        for x in range(1, parts + 1):
            y = 0
            for a in reversed(coefficients):
                y = (y * x + a) % PRIME
            shares.append((x, y))
        # Lagrange at 0 over the last `threshold` shares.
        chosen = shares[-threshold:]
        total = 0
        for i, (xi, yi) in enumerate(chosen):
            num, den = 1, 1
            for j, (xj, _) in enumerate(chosen):
                if i != j:
                    num = num * xj % PRIME
                    den = den * (xj - xi) % PRIME
            total = (total + yi * num * pow(den, PRIME - 2, PRIME)) % PRIME
        assert total == coefficients[0]
        rows.append([str(threshold), secret.hex(), random.hex(),
                     ",".join(f"{x}:{y.to_bytes(66, 'big').hex()}" for x, y in shares)])
    return rows


def main() -> int:
    cases = gf_cases()
    shares = run_vault(cases)
    gf_rows = []
    for case, share_list in zip(cases, shares):
        gf_rows.append([str(case["threshold"]), ",".join(str(x) for x in case["xs"]), case["secret"],
                        case["random"], ",".join(share_list),
                        ";".join(",".join(str(i) for i in s) for s in case["subsets"])])
    header = ["Shamir secret sharing known answers. Source: scripts/crypto-vectors/gen-advanced-sss.py.",
              f"GF(2^8): HashiCorp Vault shamir/shamir.go at commit {VAULT_COMMIT} (Combine checks every subset).",
              "Prime field 2^521 - 1: the generator's Python reference."]
    path = write_module("crypto-advanced", "sss-vectors.nra", "SssVectors", header, [
        ("gf256", ["threshold", "xs", "secret", "random", "shares", "subsets"], gf_rows),
        ("prime", ["threshold", "secret", "random", "x:y shares"], prime_cases()),
    ])
    print(f"wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

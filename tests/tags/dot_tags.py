#!/usr/bin/env python3
"""Check compiler, artifact, source-doc, diagnostic and formatter parity of tag sigils."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

NEURA = Path(sys.argv.pop(1)).resolve()
FIXTURES = Path(__file__).resolve().parent
TIMEOUT = 120 * int(os.environ.get("NEURA_TEST_TIMEOUT_SCALE", "1"))


class DotTagTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory(prefix="dot-tags-", dir=Path.cwd())
        self.addCleanup(scratch.cleanup)
        self.work = Path(scratch.name)
        self.source = self.work / "program.nra"

    def neura(self, *arguments):
        return subprocess.run(
            [str(NEURA), *arguments], cwd=self.work, capture_output=True, timeout=TIMEOUT,
        )

    def require_success(self, *arguments):
        result = self.neura(*arguments)
        self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8"))
        return result.stdout

    def observe(self, spelling):
        # Keep the same name, location and byte widths, including for the source map.
        self.source.write_bytes((FIXTURES / f"{spelling}-tags.nra").read_bytes())
        self.require_success("check", "program.nra", "--error-format=short")
        ir = self.require_success("emit-ir", "program.nra")
        plain_ir = self.require_success("emit-ir", "--no-optimize", "program.nra")
        self.require_success("build", "program.nra", "-o", "program.nvr")
        image = (self.work / "program.nvr").read_bytes()
        docs = self.require_success("docs", "program.nra")
        self.assertEqual(self.require_success(
            "run", "--runtime=off", "--tier=0", "--gpu=off", "program.nvr",
        ).splitlines(), [b"15"])
        return {"IR": ir, "unoptimized IR": plain_ir, "image": image, "docs": docs}

    def test_tag_spellings_produce_identical_ir_images_and_docs(self):
        reference = self.observe("hash")
        for spelling in ("dot", "mixed"):
            with self.subTest(spelling=spelling):
                observed = self.observe(spelling)
                for output, expected in reference.items():
                    self.assertTrue(observed[output] == expected, f"{output} bytes differ from hash spelling")
        symbols = json.loads(reference["docs"])["files"][0]["symbols"]
        metrics = next(symbol for symbol in symbols if symbol["name"] == "Metrics")
        self.assertEqual(metrics["documentation"]["text"], "Reads the elapsed time with calibration.")
        self.assertEqual([tag["name"] for tag in metrics["tags"]], ["Since", "Stable", "Example"])
        self.assertNotIn("internal_count", [symbol["name"] for symbol in symbols])

    def test_tag_fixtures_are_formatted(self):
        self.require_success("fmt", "--check", *(
            str(FIXTURES / f"{spelling}-tags.nra") for spelling in ("hash", "dot", "mixed")
        ))

    def diagnostic(self, source, code, line, column, length):
        self.source.write_bytes(source.encode("utf-8"))
        short = self.neura("check", "program.nra", "--error-format=short")
        self.assertNotEqual(short.returncode, 0)
        self.assertIn(
            f"program.nra:{line}:{column}: error[{code}]:".encode("utf-8"),
            short.stderr.splitlines()[0],
        )
        # Short form identifies the primary span's start. Verify the width too.
        details = self.neura("check", "program.nra", "--error-format=json")
        error = json.loads(details.stderr.splitlines()[0])
        self.assertEqual(error["code"], code)
        primary = next(span for span in error["spans"] if span["primary"])
        self.assertEqual((primary["line"], primary["column"], primary["length"]),
                         (line, column, length))
        return short.stderr

    def test_both_spellings_reject_the_same_input(self):
        cases = (
            ("@Depricated\nlet value = 1;\n", "E0900", 1, 2, 10),
            ("let read = func(@Pure value: int) -> int => value;\n", "E0901", 1, 18, 4),
            ("@[Since(version: )]\nlet value = 1;\n", "E0910", 1, 18, 1),
            ("@[]\nlet value = 1;\n", "E0910", 1, 3, 1),
            ("let count = func() {\n    @Note(\"Loop\")\n    while (false) {}\n};\n", "E0909", 3, 5, 5),
            ("@Note(\"Destructure\")\nlet [value, status] = [1, true];\n", "E0909", 2, 1, 3),
            ("@Note(\"Discard\")\nlet _ = 1;\n", "E0909", 1, 1, 16),
            ("let Session = class (\n    @Deprecated\n    | get: instance;\n);\n", "E0215", 2, 5, 11),
        )
        for source, *location in cases:
            with self.subTest(source=source):
                hash_error = self.diagnostic(source.replace("@", "#"), *location)
                dot_error = self.diagnostic(source.replace("@", "."), *location)
                self.assertEqual(hash_error, dot_error)

    def test_dot_opener_needs_a_name_or_a_group(self):
        self.diagnostic(".\nlet value = 1;\n", "E0271", 1, 1, 1)

    def test_dot_tag_cannot_start_an_expression(self):
        self.diagnostic("let value = .Note;\n", "E0204", 1, 13, 1)


if __name__ == "__main__":
    unittest.main()

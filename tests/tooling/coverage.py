#!/usr/bin/env python3
"""Coverage contract tests against the built toolchain, without synthetic hits."""
from __future__ import annotations
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

NEURA = pathlib.Path(sys.argv.pop(1)).resolve()


class Coverage(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="neura-coverage-")
        self.directory = pathlib.Path(self.temp.name).resolve()
        self.source = self.directory / "coverage.nra"
        self.source.write_text('''let choose = func(flag: bool) -> int32 {
    if (flag) {
        return 10;
    }
    return 20;
};
let never_called = func() -> int32 {
    return 30;
};
let Examples = module (
    export let test_first = func() -> void {
        let value = choose(true);
    };
    export let test_second = func() -> void {
        let value = choose(true);
    };
);
''', encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def invoke(self, *args, expected=0):
        result = subprocess.run([str(NEURA), *map(str, args)], cwd=self.directory,
                                capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, expected, result.stdout + "\n" + result.stderr)
        return result

    def collect(self, *args, name="coverage.json"):
        output = self.directory / name
        result = self.invoke("test", "--reporter=json", "--coverage", output, *args, self.source)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        event = next(item for item in events if item["event"] == "coverage")
        self.assertEqual(pathlib.Path(event["path"]), output)
        self.assertEqual(event["granularity"], "mapped-block-line")
        self.assertEqual(events[-1]["event"], "summary")
        value = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(value["version"], 1)
        self.assertEqual(value["granularity"], "mapped-block-line")
        self.assertEqual(value["measurement"], "blockEntries")
        file = next(item for item in value["files"] if pathlib.Path(item["path"]) == self.source)
        self.assertNotIn("branches", file)
        return {item["line"]: item["count"] for item in file["statements"]}

    def test_taken_untaken_and_uncalled_locations(self):
        counts = self.collect()
        self.assertGreater(counts[3], 0, "taken branch must be observed by the NVM")
        self.assertEqual(counts[5], 0, "untaken branch must remain unexecuted")
        self.assertEqual(counts[8], 0, "uncalled concrete project functions must appear with zero hits")

    def test_counts_aggregate_fresh_test_executions(self):
        discovery = json.loads(self.invoke("test", "--list", "--format=json", self.source).stdout)
        selected = discovery["tests"][0]["id"]
        one = self.collect("--filter", selected, name="one.json")
        both = self.collect(name="both.json")
        self.assertGreater(one[3], 0)
        self.assertEqual(both[3], 2 * one[3])
        self.assertEqual(both[5], 0)
        self.assertEqual(both[8], 0)

    def test_coverage_uses_reference_cpu_despite_optimized_profile(self):
        counts = self.collect("--tier=2", "--jit=on", "--gpu=always")
        self.assertGreater(counts[3], 0)
        self.assertEqual(counts[5], 0)

    def test_coverage_output_cannot_overwrite_source(self):
        original = self.source.read_bytes()
        result = self.invoke("test", "--coverage", self.source, self.source, expected=1)
        self.assertIn("must not replace a source file", result.stderr)
        self.assertEqual(self.source.read_bytes(), original)

    def test_discovery_does_not_write_coverage(self):
        output = self.directory / "not-written.json"
        result = self.invoke("test", "--list", "--format=json", "--coverage", output, self.source, expected=1)
        self.assertIn("cannot be combined with --list", result.stderr)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()

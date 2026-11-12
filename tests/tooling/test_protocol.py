#!/usr/bin/env python3
"""Real CLI contract tests: python3 tests/tooling/test_protocol.py build/neura."""
from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

NEURA = pathlib.Path(sys.argv.pop(1)).resolve()
ROOT = pathlib.Path(__file__).resolve().parents[2]


class TestProtocol(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="neura-test-protocol-")
        self.directory = pathlib.Path(self.temp.name).resolve()
        self.source = self.directory / "suite with spaces.nra"
        self.source.write_text('''let check = import:Std.Test;
let console = import:Std.Io.Console;
let initialize = func() -> int32 {
    console.print_line("INITIALIZED");
    return 0;
};
let Suite = module (
    let count: int32 = initialize();
    export let test_first = func() -> void {
        count += 1;
        check.assert_equal_int32(1, count);
        console.print_line("quoted \\\"output\\\" 💙");
    };
    export let test_second = func() -> void {
        count += 1;
        check.assert_equal_int32(1, count);
    };
    export let test_failure = func() -> void {
        check.assert_equal_int32(3, 4);
    };
    let test_hidden = func() -> void {};
    export let test_argument = func(value: int32) -> void {};
    export let test_result = func() -> int32 => 1;
    export let test_generic = func<T>() -> void {};
    export let ordinary_function = func() -> void {};
);
export let test_file_level = func() -> void {};
''', encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def invoke(self, *args, cwd=None, expected=0):
        result = subprocess.run([str(NEURA), *map(str, args)], cwd=cwd or self.directory,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                encoding="utf-8", timeout=90)
        self.assertEqual(result.returncode, expected, result.stdout + "\n" + result.stderr)
        return result

    def discover(self, *files, cwd=None):
        result = self.invoke("test", "--list", "--format=json", *(files or (self.source,)), cwd=cwd)
        value = json.loads(result.stdout)
        self.assertEqual(value["version"], 1)
        return value["tests"]

    def events(self, *args, expected=0, cwd=None):
        result = self.invoke("test", "--reporter=json", *args, expected=expected, cwd=cwd)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertTrue(events)
        self.assertTrue(all(event["version"] == 1 for event in events))
        return events

    def test_discovery_predicate_locations_and_no_execution(self):
        tests = self.discover()
        self.assertEqual([item["name"] for item in tests],
                         ["Suite.test_first", "Suite.test_second", "Suite.test_failure"])
        self.assertEqual(tests[0]["id"], "suite with spaces.nra::Suite.test_first")
        self.assertEqual(pathlib.Path(tests[0]["file"]), self.source)
        self.assertGreater(tests[0]["line"], 0)
        self.assertGreater(tests[0]["column"], 0)
        self.assertGreater(tests[0]["length"], 0)
        self.assertEqual(tests, self.discover())
        self.source.write_text("\n\n" + self.source.read_text(encoding="utf-8"), encoding="utf-8")
        self.assertEqual([test["id"] for test in tests], [test["id"] for test in self.discover()])

    def test_exact_filter_and_attributed_output(self):
        test = self.discover()[0]
        events = self.events("--filter", test["id"], self.source)
        starts = [item for item in events if item["event"] == "testStart"]
        results = [item for item in events if item["event"] == "testResult"]
        self.assertEqual([item["id"] for item in starts], [test["id"]])
        self.assertEqual([item["id"] for item in results], [test["id"]])
        self.assertEqual(results[0]["state"], "passed")
        self.assertGreaterEqual(results[0]["duration"], 0)
        output = "".join(item["output"] for item in events if item["event"] == "output")
        self.assertIn("INITIALIZED", output)
        self.assertIn('quoted "output" 💙', output)
        self.assertTrue(all(item["id"] == test["id"] for item in events if item["event"] == "output"))
        self.assertEqual(events[-1], {"version": 1, "event": "summary", "total": 1, "passed": 1, "failed": 0})

    def test_suite_fresh_state_failures_and_source_location(self):
        events = self.events(self.source, expected=1)
        results = [item for item in events if item["event"] == "testResult"]
        self.assertEqual([item["state"] for item in results], ["passed", "passed", "failed"])
        self.assertIn("expected", results[-1]["message"])
        self.assertEqual(pathlib.Path(results[-1]["location"]["file"]), self.source)
        lines = self.source.read_text(encoding="utf-8").splitlines()
        failing_line = next(i for i, line in enumerate(lines, 1) if "(3, 4)" in line)
        self.assertEqual(results[-1]["location"]["line"], failing_line)
        self.assertEqual(events[-1]["total"], 3)
        self.assertEqual(events[-1]["passed"], 2)
        self.assertEqual(events[-1]["failed"], 1)

    def test_filters_deduplicate_and_unknown_id_never_runs_suite(self):
        tests = self.discover()
        events = self.events("--filter", tests[0]["id"], "--filter=" + tests[0]["id"],
                             "--filter", tests[1]["id"], self.source)
        self.assertEqual(events[-1]["total"], 2)
        result = self.invoke("test", "--reporter=json", "--filter", "missing::Suite.test_no", self.source, expected=1)
        self.assertEqual(result.stdout, "")
        self.assertIn("unknown test id", result.stderr)

    def test_imported_tests_require_explicit_input(self):
        helper = self.directory / "helpers.nra"
        helper.write_text("let Helpers = module (export let test_helper = func() -> void {};);\n", encoding="utf-8")
        self.source.write_text("let helpers = import:Helpers;\n" + self.source.read_text(encoding="utf-8"), encoding="utf-8")
        self.assertEqual(len(self.discover()), 3)
        tests = self.discover(self.source, helper)
        self.assertEqual(len(tests), 4)
        self.assertTrue(any(item["name"] == "Helpers.test_helper" for item in tests))

    def test_workspace_root_keeps_ids_stable_with_another_execution_cwd(self):
        tests = self.discover()
        result = self.invoke("test", "--list", "--format=json", "--workspace-root", self.directory,
                             self.source, cwd=ROOT)
        self.assertEqual([item["id"] for item in json.loads(result.stdout)["tests"]],
                         [item["id"] for item in tests])
        events = self.events("--workspace-root", self.directory, "--filter", tests[0]["id"],
                             self.source, cwd=ROOT)
        self.assertEqual(events[-1]["passed"], 1)

    def test_empty_discovery_succeeds(self):
        self.source.write_text("let Empty = module (let helper = func() -> void {};);\n", encoding="utf-8")
        self.assertEqual(self.discover(), [])
        self.invoke("test", self.source, expected=1)

    def test_unicode_output_chunk_boundary(self):
        output = "a" * 16383 + "💙" + "b" * 10
        self.source.write_text('let console = import:Std.Io.Console;\nlet Large = module (\n'
                               'export let test_output = func() -> void {console.print_line("' + output + '");};\n);\n', encoding="utf-8")
        events = self.events(self.source)
        actual = "".join(item["output"] for item in events if item["event"] == "output")
        self.assertEqual(actual, output + "\n")

    def test_human_reporter_keeps_existing_text(self):
        result = self.invoke("test", self.source, expected=1)
        self.assertIn("PASS Suite.test_first", result.stdout)
        self.assertIn("FAIL Suite.test_failure", result.stdout)
        self.assertIn("3 test(s), 2 passed, 1 failed", result.stdout)

    def test_schedule_failure_reproduction(self):
        source = ROOT / "tests/concurrency/schedules/order-tests.nra"
        tests = self.discover(source, cwd=ROOT)
        target = next(item for item in tests if item["name"].endswith("test_assumes_spawn_order"))
        saved = self.directory / "failing schedule.txt"
        events = self.events("--filter", target["id"], "--schedules", 8, "--schedule-seed", 1,
                             "--save-schedule", saved, source, expected=1, cwd=ROOT)
        result = next(item for item in events if item["event"] == "testResult")
        self.assertEqual(pathlib.Path(result["savedSchedule"]), saved)
        self.assertTrue(saved.is_file())
        replay = self.events("--schedule", saved, source, expected=1, cwd=ROOT)
        results = [item for item in replay if item["event"] == "testResult"]
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["id"], target["id"])
        self.assertEqual(replay[-1]["failed"], 1)


if __name__ == "__main__":
    unittest.main()

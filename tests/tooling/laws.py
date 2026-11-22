#!/usr/bin/env python3
"""Law proofs through the SDK command, formatter, editor and source docs."""

import json
import pathlib
import subprocess
import tempfile
import unittest

from lsp_protocol import BINARY, Client, params

FORMATTED = """let Boolean = module (
    /// Identity of a Boolean value.
    export let Identity = law (
        for flag: bool;
        claim: { flag == flag : bool };
    );
        | proof: {==};
);
"""
UNFORMATTED = """let Boolean=module(
/// Identity of a Boolean value.
export let Identity=law(for flag:bool;claim:{flag==flag:bool};);|proof:{ == };);
"""
INVALID = FORMATTED.replace("flag == flag", "flag == not flag")


class LawToolingTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="neura-law-tooling-")
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name).resolve()
        self.file = self.root / "law.nra"
        self.file.write_text(FORMATTED, encoding="utf-8")

    def command(self, *arguments, check=True):
        result = subprocess.run([str(BINARY), *map(str, arguments)], cwd=self.root,
                                capture_output=True, text=True, timeout=30, encoding="utf-8")
        if check:
            self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def test_prove_verdict_and_nir_round_trip(self):
        human = self.command("prove", self.file)
        self.assertIn("PROVEN   Boolean.Identity", human.stdout)
        self.assertIn("Verdict: 1 proven, 0 abstract", human.stdout)
        expected = {
            "status": "ok",
            "laws": [{"name": "Boolean.Identity", "status": "PROVEN"}],
            "proven": 1, "abstract": 0, "axioms": 0,
        }
        self.assertEqual(json.loads(self.command("prove", "--error-format", "json", self.file).stdout), expected)
        self.assertEqual(self.command("prove", "--error-format=short", self.file).stdout,
                         "PROVEN Boolean.Identity\n")

        nir = self.root / "law.nir"
        self.command("emit-ir", self.file, "-o", nir)
        result = self.command("prove", "--proof-fuel=1000000", "--error-format=json", nir)
        self.assertEqual(json.loads(result.stdout), expected)

    def test_prove_rejection_and_deferred_options(self):
        self.file.write_text(INVALID, encoding="utf-8")
        failure = self.command("prove", "--error-format=json", self.file, check=False)
        self.assertEqual(failure.returncode, 1)
        self.assertEqual(json.loads(failure.stdout)["status"], "failed")
        diagnostic = json.loads(failure.stderr.splitlines()[0])
        self.assertEqual(diagnostic["code"], "E1401")
        for option in ("--laws", "--verdict", "--allow-incomplete"):
            with self.subTest(option=option):
                deferred = self.command("prove", option, self.file, check=False)
                self.assertEqual(deferred.returncode, 1)
                self.assertIn(option, deferred.stderr)
                self.assertIn("later release", deferred.stderr)

    def test_formatter_is_idempotent(self):
        self.file.write_text(UNFORMATTED, encoding="utf-8")
        self.assertEqual(self.command("fmt", self.file).stdout, FORMATTED)
        self.command("fmt", "--write", self.file)
        self.command("fmt", "--check", self.file)
        self.command("check", self.file)

    def test_documentation_shows_statement_without_certificate(self):
        result = self.command("docs", self.file)
        index = json.loads(result.stdout)
        symbols = index["files"][0]["symbols"]
        law = next(item for item in symbols if item["name"] == "Identity")
        self.assertEqual(law["kind"], "law")
        self.assertIn("for flag: bool;", law["signature"])
        self.assertIn("claim: { flag == flag : bool };", law["signature"])
        self.assertNotIn("proof", law["signature"])
        self.assertNotIn("{==}", law["signature"])

    def test_editor_sees_law_symbol_hover_and_proof_diagnostic(self):
        client = Client(self.root)
        self.addCleanup(client.close)
        client.open(self.file, FORMATTED)
        symbols = client.request("textDocument/documentSymbol", params(self.file))
        law = next(item for item in symbols[0]["children"] if item["name"] == "Identity")
        self.assertEqual(law["kind"], 14)
        self.assertEqual(law["selectionRange"]["start"], {"line": 2, "character": 15})
        self.assertIn("flag", [item["name"] for item in law["children"]])
        hover = client.request("textDocument/hover", params(self.file, 2, 16))["contents"]["value"]
        self.assertIn("claim: { flag == flag : bool };", hover)
        self.assertIn("Identity of a Boolean value.", hover)
        self.assertNotIn("| proof", hover)
        client.notify("textDocument/didChange", {
            "textDocument": {"uri": self.file.as_uri(), "version": 2},
            "contentChanges": [{"text": INVALID}],
        })
        client.request("textDocument/documentSymbol", params(self.file))
        reports = [notification["params"]["diagnostics"] for notification in client.notifications
                   if notification.get("method") == "textDocument/publishDiagnostics"
                   and notification["params"]["uri"] == self.file.as_uri()]
        self.assertEqual(reports[0], [])
        diagnostic = next(item for item in reports[-1] if item["code"] == "E1401")
        self.assertEqual(diagnostic["range"]["start"], {"line": 6, "character": 17})
        self.assertEqual(diagnostic["range"]["end"], {"line": 6, "character": 21})

    def test_explain_all_law_diagnostics(self):
        for number in range(1401, 1422):
            code = f"E{number}"
            with self.subTest(code=code):
                result = self.command("explain", code)
                self.assertTrue(result.stdout.startswith(f"# {code}: "), result.stdout)


if __name__ == "__main__":
    unittest.main()

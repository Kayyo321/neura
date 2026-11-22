#!/usr/bin/env python3
"""Check Phase 1 laws at the NeuraIR and image boundaries."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


NEURA = Path(sys.argv.pop(1)).resolve()

BASE = """\
export let Base = module (
    .Pure
    export let supported = func() -> bool;

    export let Supported = law (
        last expected = true;
        claim: { supported() == expected : bool };
    );
);
"""

PROVED = """\
export let Verified = module (
    .Pure
    export let value = func() -> bool => true;

    export let AlwaysTrue = law (
        claim: { value() == true : bool };
    );
        | proof: {==};
);
"""

ENTRY = """\
let program = import:Std.Program;

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        return program.ExitCode.Ok;
    };
);
"""


class Laws(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.directory = Path(directory.name)

    def write(self, path, contents):
        file = self.directory / path
        file.write_text(contents, encoding="utf-8")
        return file

    def neura(self, *args, status=0):
        result = subprocess.run(
            [str(NEURA), *map(str, args)], cwd=self.directory,
            env={**os.environ, "NEURA_GPU": "off"},
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=60,
        )
        self.assertEqual(result.returncode, status, result.stderr)
        return result

    def emit(self, source, name):
        artifact = self.directory / name
        self.neura("emit-ir", source, "-o", artifact)
        self.assertEqual(artifact.read_bytes()[:8], b"NEIR\x01\x00\x10\x00")
        return artifact

    def test_certificates_and_inherited_obligations_across_artifacts(self):
        self.emit(self.write("base.nra", BASE), "base.nir")
        self.emit(self.write("verified.nra", PROVED), "verified.nir")
        app = self.write("proofs.nra", """\
let base = import:"base.nir".Base;
let verified = import:"verified.nir".Verified;

let Implementation = module:base (
    .Pure
    export let.override supported = func() -> bool => true;

    let.proof Supported: {
        last value = expected;
        have direct: { supported() == value : bool } = {==};
        direct
    };

    let Imported = law (
        claim: { verified.value() == true : bool };
    );
        | proof: verified.AlwaysTrue();
);
""")
        verdict = self.neura("prove", app).stdout
        self.assertIn("PROVEN   Implementation.Imported", verdict)
        combined = self.emit(app, "combined.nir")
        self.assertEqual(self.neura("prove", combined).stdout, verdict)

        contents = app.read_text()
        start = contents.index("    let.proof Supported")
        end = contents.index("    let Imported", start)
        missing = self.write("missing.nra", contents[:start] + contents[end:])
        result = self.neura("build", "--error-format=short", missing, "-o", "missing.nvr", status=1)
        self.assertIn("error[E1404]", result.stderr)

        changed = self.write("changed.nra", """\
let verified = import:"verified.nir".Verified;

let Bad = module:verified (
    .Pure
    export let.override value = func() -> bool => false;
);
""")
        error = self.neura("check", "--error-format=short", changed, status=1).stderr
        self.assertIn("changed.nra:5:25: error[E1406]", error)
        self.assertIn("Verified.AlwaysTrue", error)

    def test_reader_rechecks_certificate(self):
        source = self.write("usable.nra", ENTRY + """\
let Proofs = module (
    let Trivial = law (
        claim: { true == true : bool };
    );
        | proof: {==};
);
""")
        marker = self.emit(source, "usable.nir")
        image_from_source = self.directory / "source.nvr"
        image_from_ir = self.directory / "artifact.nvr"
        self.neura("build", source, "-o", image_from_source)
        self.neura("build", marker, "-o", image_from_ir)
        self.assertEqual(image_from_source.read_bytes(), image_from_ir.read_bytes())

        # A leaf reflexivity certificate has a 1-byte kind, a span, and
        # six u32 fields including the three empty vector counts.
        raw = bytearray(marker.read_bytes())
        proof_start = len(raw) - (1 + 36 + 6 * 4)
        self.assertEqual(raw[proof_start], 2)
        raw[proof_start] = 3  # Empty Block, which cannot discharge the goal.
        damaged = self.directory / "damaged.nir"
        damaged.write_bytes(raw)
        error = self.neura("build", "--error-format=short", damaged, "-o", "damaged.nvr", status=1).stderr
        self.assertIn("error[E1403]", error)

        raw[6] = 15
        wrong_version = self.directory / "wrong-version.nir"
        wrong_version.write_bytes(raw)
        self.assertIn("invalid symbol kind tag", self.neura("prove", wrong_version, status=1).stderr)

    def test_laws_erase_without_changing_image(self):
        source = self.write("app.nra", ENTRY)
        first = self.directory / "before.nvr"
        second = self.directory / "after.nvr"
        self.neura("build", source, "-o", first)
        source.write_text(ENTRY + """\
let Logic = module (
    let BooleanReflexivity = law (
        for flag: bool;
        claim: { flag == flag : bool };
    );
        | proof: {==};
);
""", encoding="utf-8")
        self.neura("build", source, "-o", second)
        self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_executable_module_cannot_leave_its_own_law_abstract(self):
        source = self.write("entry.nra", ENTRY.replace(
            "    export let.override main",
            "    let Obligation = law (holds: true;);\n    export let.override main",
        ))
        failure = self.neura("build", "--error-format=short", source, "-o", "failed.nvr", status=1)
        self.assertIn("entry.nra:4:5: error[E1403]", failure.stderr)
        self.assertIn("App.Obligation", failure.stderr)

    def test_fuel_has_source_span_and_budget(self):
        source = self.write("fuel.nra", """\
let Logic = module (
    let Reflexive = law (
        claim: { true == true : bool };
    );
        | proof: {==};
);
""")
        self.assertIn("PROVEN   Logic.Reflexive", self.neura("prove", source).stdout)
        limited = self.neura("prove", "--proof-fuel", "1", "--error-format=json", source, status=1)
        diagnostic = next(json.loads(line) for line in limited.stderr.splitlines() if line.startswith("{"))
        self.assertEqual(diagnostic["code"], "E1415")
        self.assertIn("budget 1", diagnostic["message"])
        self.assertIn("fuel.nra:3:18:", self.neura(
            "prove", "--proof-fuel", "1", "--error-format=short", source, status=1,
        ).stderr)

    def test_law_reference_as_proposition(self):
        source = self.write("references.nra", """\
let Logic = module (
    let Alias = law (
        for flag: bool;
        claim: Reflexive(flag);
    );
        | proof: Reflexive(flag);

    let Reflexive = law (
        for flag: bool;
        claim: { flag == flag : bool };
    );
        | proof: {==};
);
""")
        checked = self.neura("prove", source)
        self.assertIn("PROVEN   Logic.Alias", checked.stdout)

    def test_independent_verdict_is_deferred(self):
        source = self.write("proved.nra", PROVED)
        result = self.neura("prove", "--verdict", source, status=1)
        self.assertIn("independent --verdict proof checker", result.stderr)
        self.assertIn("later release", result.stderr)


if __name__ == "__main__":
    unittest.main()

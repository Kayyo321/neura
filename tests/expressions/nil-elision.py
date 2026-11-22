#!/usr/bin/env python3
"""Compare nil-sensitive runs with optimization enabled, disabled, and bisected."""

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


FIXTURES = Path(__file__).resolve().with_name("nil-elision")
TESTS = FIXTURES.parents[1]
BUILD_MODES = (
    ("unoptimized", ("--no-optimize",)),
    ("disabled", ("--no-nil-check-elision",)),
    ("optimized", ()),
)
NIL_MESSAGE = "nil class reference"
TIMEOUT = 120 * int(os.environ.get("NEURA_TEST_TIMEOUT_SCALE", "1"))


@dataclass(frozen=True)
class Case:
    args: tuple = ()
    stdout: str = ""
    status: int = 0
    message: str = ""
    file: str = ""
    line: int = 0


def trap(source, marker, name="", stdout="", status=139, message=NIL_MESSAGE):
    line, = (number for number, text in enumerate(source.read_text(encoding="utf-8").splitlines(), 1)
             if marker in text)
    return Case((name,) if name else (), stdout, status, message, source.name, line)


def tier_modes(native):
    eager = {"NEURA_TIER_EAGER": "1"}
    verify = {**eager, "NEURA_TIER3_VERIFY": "1"}
    shadow = {"NEURA_TIER_CHECK": "2"}
    yield "tier 0", ("--tier=0", "--jit=off"), {}
    yield "tier 1", ("--tier=1", "--jit=off"), eager
    yield "tier 2 interpreter", ("--tier=2", "--jit=off"), eager
    yield "tier 3 interpreter", ("--tier=2", "--jit=off"), {**verify, "NEURA_TIER3": "interp"}
    yield "reference shadow", ("--tier=2", "--jit=off"), {**verify, **shadow, "NEURA_TIER3": "interp"}
    if native:
        yield "tier 2 native", ("--tier=2", "--jit=on"), eager
        yield "tier 3 native", ("--tier=2", "--jit=on"), {**verify, "NEURA_TIER3": "native"}
        yield "native shadow", ("--tier=2", "--jit=on"), {**verify, **shadow, "NEURA_TIER3": "native"}


def run(*command, env=None):
    base = {name: value for name, value in os.environ.items()
            if not name.startswith(("NEURA_TIER", "NEURA_JIT", "NEURA_RUN_OPTIONS"))}
    return subprocess.run(command, capture_output=True, encoding="utf-8", timeout=TIMEOUT,
                          env={**base, "NEURA_GPU": "off", "NEURA_TIER3": "off", **(env or {})})


def build(neura, *arguments):
    result = run(neura, *arguments)
    assert result.returncode == 0, (arguments, result.stderr)


def verify_outcome(result, case, label):
    assert result.returncode == case.status, (label, result.returncode, result.stderr)
    assert result.stdout == case.stdout, (label, result.stdout, case.stdout)
    assert "NEURA_TIER_CHECK=2 is not applied" not in result.stderr, (label, result.stderr)
    traps = [line.split(": trap: ", 1)[1] for line in result.stderr.splitlines() if ": trap: " in line]
    if not case.message:
        assert not traps, (label, result.stderr)
        return result.returncode, result.stdout, None

    assert len(traps) == 1, (label, result.stderr)
    description, location = traps[0].rsplit(" at ", 1)
    # Block and function IDs may change with optimization. Keep the kind,
    # reason, detail (including task context) and exact source coordinate.
    message = re.sub(r" in function \d+ block \d+$", "", description)
    source, line, column = location.rsplit(":", 2)
    expected_message = message.partition(" (task ")[0].removesuffix(":")
    assert re.fullmatch(case.message, expected_message), (label, message)
    assert Path(source.replace("\\", "/")).name == case.file and int(line) == case.line, (label, location)
    assert int(column) > 0, (label, location)
    return result.returncode, result.stdout, (message, location)


def verify_image_switch(label, images, expect_difference):
    if expect_difference is None:
        return
    differing = images["optimized"].read_bytes() != images["disabled"].read_bytes()
    assert differing == expect_difference, (label, "images differ" if differing else "images are identical")


def package(neura, image):
    launcher = image.with_suffix("")
    build(neura, "build", "--native", "--deny-native", image, "-o", launcher)
    return launcher


def runtime_outcomes(neura, native, image, launcher, case, label):
    outcomes = {}
    for mode, flags, env in tier_modes(native):
        result = run(neura, "run", "--runtime=off", "--gpu=off", "--deny-native",
                     *flags, image, "--", *case.args, env=env)
        outcomes[mode] = verify_outcome(result, case, (label, mode))
    if launcher:
        mode = "native executable"
        env = {"NEURA_TIER_EAGER": "1", "NEURA_TIER3": "native", "NEURA_TIER3_VERIFY": "1"}
        result = run(launcher, *case.args, env=env)
        outcomes[mode] = verify_outcome(result, case, (label, mode))
    return outcomes


def exercise(neura, native, work, label, source, cases, expect_difference=None):
    images = {}
    launchers = {}
    for variant, flags in BUILD_MODES:
        images[variant] = work / f"{label}-{variant}.nvr"
        build(neura, "build", *flags, source, "-o", images[variant])
        if native:
            launchers[variant] = package(neura, images[variant])
    verify_image_switch(label, images, expect_difference)

    count = 0
    for case in cases:
        outcomes = {}
        for variant, image in images.items():
            context = label, case.args, variant
            outcomes[variant] = runtime_outcomes(neura, native, image, launchers.get(variant), case, context)
        reference = outcomes["unoptimized"]["tier 0"]
        for variant, modes in outcomes.items():
            for mode, outcome in modes.items():
                assert outcome == reference, (label, case.args, variant, mode, outcome, reference)
                count += 1
    return count


def verify_artifacts(neura, native, work, case):
    positive = work / "positive-optimized.nvr"
    assembly = work / "positive.nvra"
    rebuilt = work / "positive-reassembled.nvr"
    build(neura, "disassemble", positive, "-o", assembly)
    assert "exit state read-field non-nil-receiver " in assembly.read_text(encoding="utf-8")
    build(neura, "assemble", assembly, "-o", rebuilt)
    assert positive.read_bytes() == rebuilt.read_bytes(), "hinted image did not round-trip"

    original = work / "no-removable-check-disabled.nvr"
    assembly = work / "original-checked.nvra"
    build(neura, "disassemble", original, "-o", assembly)
    text = assembly.read_text(encoding="utf-8")
    # The contract-only fixture has one field read, reached with nil.
    read = "exit state read-field immediate "
    assert text.count(read) == 1 and "non-nil-receiver" not in text
    hinted = work / "incorrect-receiver-hint.nvr"
    assembly = hinted.with_suffix(".nvra")
    assembly.write_text(text.replace(read, "exit state read-field non-nil-receiver immediate "), encoding="utf-8")
    build(neura, "assemble", assembly, "-o", hinted)
    assert hinted.read_bytes() != original.read_bytes(), "the inserted hint did not reach the artifact"

    result = run(neura, "run", "--runtime=off", "--tier=0", "--jit=off", original)
    reference = verify_outcome(result, case, "unhinted artifact")
    launcher = package(neura, hinted) if native else None
    outcomes = runtime_outcomes(neura, native, hinted, launcher, case, "incorrect receiver hint")
    for mode, outcome in outcomes.items():
        assert outcome == reference, (mode, outcome, reference)

    assembly = work / "invalid-receiver-hint.nvra"
    assembly.write_text(text.replace(read, "exit state allocate-object non-nil-receiver immediate "), encoding="utf-8")
    result = run(neura, "assemble", assembly, "-o", assembly.with_suffix(".nvr"))
    assert result.returncode == 1, result.stderr
    assert "non-nil-receiver requires a nil-checked object state operation" in result.stderr, result.stderr
    return 1 + len(outcomes)


def qualify(neura, native, work):
    adversarial = FIXTURES / "adversarial.nra"
    cases = [trap(adversarial, f"// trap: {name}", name, stdout) for name, stdout in (
        ("reassignment", "17\n"), ("loop", "17\n"), ("closure", "17\n"), ("alias", ""),
        ("mutable-field", ""), ("global", ""), ("getter", ""), ("generic", ""),
        ("task-argument", ""), ("task-result", ""),
    )]
    cases.append(trap(adversarial, "// trap: explicit-guard", "explicit-guard", status=134,
                      message=r"explicit program failure \(trap reason 3\): missing item"))
    cases.append(trap(adversarial, "// trap: destroyed", "destroyed",
                      message=r"invalid reference handle: handle \d+"))
    count = exercise(neura, native, work, "adversarial", adversarial, cases)

    library = FIXTURES / "library.nra"
    dispatch = FIXTURES / "dispatch.nra"
    cases = [trap(library, f"// trap: {name}", name) for name in ("virtual", "interface")]
    cases.append(Case(("guarded",), stdout="0\n"))
    count += exercise(neura, native, work, "source-dispatch", dispatch, cases)
    shutil.copyfile(dispatch, work / "dispatch.nra")
    build(neura, "emit-ir", library, "-o", work / "library.nir")
    count += exercise(neura, native, work, "linked-dispatch", work / "dispatch.nra", cases)

    positive = FIXTURES / "positive.nra"
    count += exercise(neura, native, work, "positive", positive, [Case(stdout="17 0 17\n")], True)
    unchanged = FIXTURES / "no-removable-check.nra"
    contract = trap(unchanged, "// trap: contract")
    count += exercise(neura, native, work, "no-removable-check", unchanged, [contract], False)
    count += verify_artifacts(neura, native, work, contract)

    conditions = TESTS / "expressions" / "nil-conditions" / "conditions.nra"
    class_nil = TESTS / "backend" / "programs" / "class-nil.nra"
    receiver = TESTS / "backend" / "traps" / "nil-receiver.nra"
    acquire = TESTS / "lifetime" / "programs" / "using-nil.nra"
    count += exercise(neura, native, work, "existing-conditions", conditions, [Case(stdout="50\n3\n")])
    count += exercise(neura, native, work, "existing-class-nil", class_nil,
                      [Case(stdout="class references and value members passed\n")])
    count += exercise(neura, native, work, "existing-nil-receiver", receiver,
                      [trap(receiver, "reference.to_string()")])
    count += exercise(neura, native, work, "existing-using-nil", acquire,
                      [trap(acquire, "using (source", stdout="body\nclosed\nfirst done\n")])
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("neura", type=Path)
    parser.add_argument("--native", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="neura-nil-elision-") as temporary:
        count = qualify(args.neura.resolve(), args.native, Path(temporary))
    print(f"{count} runs matched the unoptimized tier 0 result and trap location")


if __name__ == "__main__":
    main()

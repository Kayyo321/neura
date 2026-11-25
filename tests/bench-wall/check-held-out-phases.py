#!/usr/bin/env python3
"""Check held-out timing recovery with synthetic admissions and samples."""

import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("held_out_phases", ROOT / "scripts/bench-held-out-phases.py")
PHASES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PHASES)
ARGS = SimpleNamespace(runs=2, cpu7=True)


def sample(struck=False):
    return {"seconds": 1.0, "cpu_seconds": 0.95 if struck else 1.0}


def run_pair(folder):
    next_admissions = iter([False, False, True])
    calls = []

    def admit(_):
        return {"idle_enough": next(next_admissions, True)}

    def action(label, directory):
        calls.append(directory.name)
        return sample(struck=directory.name == "0-0-before")

    clean, verified, schedule = PHASES.forks(PHASES.SDK_LABELS, ARGS, folder, action, lambda record: [record], {}, admit=admit)
    assert calls == ["0-0-before", "0-0-after", "1-0-after", "1-0-before", "0-1-before"]
    assert schedule["status"] == "complete" and schedule["extra_timed_forks"] == 1
    assert schedule["attempts"][0]["admissions"] == [{"idle_enough": x} for x in (False, False, True)]
    assert verified["before"][0]["timing_state"] == "struck"
    assert [row["fork"] for row in clean["before"]] == [0, 1]
    assert PHASES.metrics(clean, schedule) == {"wall_ratio": 1, "cpu_ratio": 1}


def check_source_phase_boundary(folder):
    clock_keys = ("source_through_encoding_seconds", "source_through_encoding_cpu_seconds")

    def action(label, directory):
        # The before fork is disturbed only after encoding. The after fork
        # has a gap through encoding that whole-process CPU can hide.
        record = sample(struck=directory.name == "0-0-before")
        record.update(zip(clock_keys, (0.5, 0.45 if directory.name == "0-0-after" else 0.5)))
        return record

    def admit(_):
        return {"idle_enough": True}

    def samples(record):
        return [record]

    action_args = (PHASES.SDK_LABELS, ARGS)
    _, source_rows, source_schedule = PHASES.forks(
        *action_args, folder / "source", action, samples, {}, admit=admit, clock_keys=clock_keys)
    _, cold_rows, cold_schedule = PHASES.forks(
        *action_args, folder / "cold", action, samples, {}, admit=admit)
    assert source_schedule["status"] == cold_schedule["status"] == "complete"
    assert source_schedule["extra_timed_forks"] == cold_schedule["extra_timed_forks"] == 1
    assert source_rows["before"][0]["disturbed"] is False
    assert source_rows["after"][0]["disturbed"] is True
    assert cold_rows["before"][0]["disturbed"] is True
    assert cold_rows["after"][0]["disturbed"] is False
    saved = json.loads((folder / "source" / "0-0-before" / "record.json").read_text(encoding="utf-8"))
    assert saved == source_rows["before"][0]
    assert (saved["seconds"], saved["cpu_seconds"]) == (1.0, 0.95)
    assert tuple(saved[key] for key in clock_keys) == (0.5, 0.5)


def run_application(folder):
    labels = (*PHASES.SDK_LABELS, "java", "csharp")
    calls = []

    def action(label, directory):
        calls.append(directory.name)
        return sample(struck=directory.name.split("-")[1] == "0")

    clean, verified, schedule = PHASES.forks(labels, ARGS, folder, action, lambda record: [record], {},
                                             limit_extra_forks=True, admit=lambda _: {"idle_enough": True})
    assert calls == [f"{i}-0-{label}" for i, order in enumerate((labels, labels[::-1])) for label in order] + [
        "0-1-before", "0-1-after"]
    assert len(schedule["owed_slots"]) == 6 and schedule["extra_timed_forks"] == 2
    assert schedule["status"] == "owed" and PHASES.metrics(clean, schedule) is None
    assert all(rows for rows in verified.values())


def run_denied(folder):
    calls = []

    def action(label, directory):
        calls.append(directory.name)
        return sample()

    clean, verified, schedule = PHASES.forks(PHASES.SDK_LABELS, ARGS, folder, action, lambda record: [record], {},
                                             admit=lambda _: {"idle_enough": False})
    assert calls == ["diagnostic-before", "diagnostic-after"]
    assert len(schedule["owed_slots"]) == 4 and schedule["extra_timed_forks"] == 0
    assert all(attempt["state"] == "not_admitted" and len(attempt["admissions"]) == 3
               for attempt in schedule["attempts"])
    assert all(rows[0]["timing_state"] == "diagnostic_only" for rows in verified.values())
    assert PHASES.metrics(clean, schedule) is None


def check_compile_overlap():
    def event(thread, kind, start, end, cpu):
        return {"thread": thread, "kind": kind, "start_ns": start, "end_ns": end, "cpu_ns": cpu}

    observation = PHASES.replay_summary([
        event(0, "initial", 0, 10, 7), event(0, "lock_wait", 4, 6, 1),
        event(0, "inline_fallback", 6, 8, 2), event(1, "lazy_entry", 3, 12, 6),
    ])
    assert observation["compiler_cpu_ns"] == 12
    assert observation["compiler_wall_union_ns"] == 12
    assert observation["compiler_wall_sum_over_threads_ns"] == 17
    assert observation["compiler_wall_parallel_overlap_ns"] == 5
    assert observation["lock_waits"] == 1


def check_partial_gate(folder):
    def row(name, ratio):
        return {"fixture": {"name": name, "sha256": {name: name}}, "extension": None, "expected_stdout_sha256": name,
                "ratios": {"startup_budget": {"wall_ratio": ratio} if ratio is not None else None}}

    shared = {"tier": "2", "jit": "on", "workers": 4, "runs": 2, "warmups": 3, "samples": 2,
              "fixture_revision": "frozen", "sdk_ids": {"before": "A"}, "machine": {}, "cpu7_preflight": {"affinity": [7]},
              "finished": True, "selected_frozen_ten": True, "startup_budget_geomean": None,
              "driver_sha256": {"driver": "driver-digest", "shared": "shared-digest"},
              "manifest_sha256": {"held-out.json": "manifest-digest"},
              "source_inputs": {name: {"sha256": name, "revision": "frozen"} for name in ("failure", "owed")}}
    baseline = {**shared, "comparison": "A/A", "results": [row("failure", 1.01), row("owed", None)]}
    candidate = {**shared, "comparison": "A/B", "results": [row("failure", 1.05), row("owed", 1.01)]}
    receipt = folder / "aa.json"
    receipt.write_text(json.dumps(baseline), encoding="utf-8")
    report = PHASES.qualification(candidate, SimpleNamespace(aa_results=receipt))
    assert report["status"] == "failed" and report["pass"] is False
    assert [row["pass"] for row in report["rows"]] == [False, None]
    assert report["pooled"]["pass"] is None
    candidate["results"][0] = row("failure", 1.02)
    report = PHASES.qualification(candidate, SimpleNamespace(aa_results=receipt))
    assert report["status"] == "owed" and report["pass"] is None

    candidate.update(results=[row("failure", 1.02)], selected_frozen_ten=False)
    report = PHASES.qualification(candidate, SimpleNamespace(aa_results=receipt))
    assert report["status"] == "partial" and report["pass"] is None and report["pooled"] is None
    assert report["rows"][0]["pass"] is True

    for changed, message in (({"driver_sha256": {"shared": "different"}}, "shared logic differs"),
                             ({"source_inputs": {}}, "unmatched A/A frozen source"),
                             ({"manifest_sha256": {"held-out.json": "different"}}, "manifest digest differs")):
        receipt.write_text(json.dumps({**baseline, **changed}), encoding="utf-8")
        try:
            PHASES.qualification(candidate, SimpleNamespace(aa_results=receipt))
        except SystemExit as error:
            assert message in str(error)
        else:
            raise AssertionError("incompatible A/A provenance was accepted")


def check_tier_witness():
    assert [(tier, jit, tier3) for _, tier, jit, tier3 in PHASES.VERIFY_MODES] == [
        ("0", "off", "off"), ("1", "off", "off"), ("2", "off", "off"), ("2", "on", "off"), ("2", "on", "native")]
    region = b"tier: regions 1 (2 blocks, 0 checks eliminated, 0 formed again); entered 4, refused 0, left 4\n"
    interpreted = PHASES.tier_observation(region + b"tier: jit off\n")
    native = PHASES.tier_observation(region + b"tier: jit on: 1 regions compiled (8 bytes of code), 0 on the interpreter; entries native 4, interpreted 0\n")
    assert interpreted["region_entries"] == native["region_entries"] == 4
    assert interpreted["jit_mode"] == "off" and interpreted["jit_native_entries"] == 0
    assert native["jit_mode"] == "on" and native["jit_regions_compiled"] == 1 and native["jit_native_entries"] == 4


def check_suite_stderr():
    expected = b"test stderr\n"
    first_warning = b"warning from SDK one\n"
    second_warning = b"warning from SDK two\n"
    for warning in (first_warning, second_warning):
        assert PHASES.suite_execution_stderr(expected + warning, warning, "test") == expected
    events = [{"id": "test", "event": "output", "stream": "stderr", "output": expected.decode()}]
    PHASES.validate_ordered_test_output(events, b"PASS test\n", expected, "test")
    try:
        PHASES.validate_execution_streams(b"PASS test\n", b"wrong stderr", b"PASS test\n", expected, "test")
    except SystemExit as error:
        assert "stderr differs from ordinary CLI execution" in str(error)
    else:
        raise AssertionError("changed suite execution stderr was accepted")


def check_stop_during_spawn(folder):
    stop = folder / "STOP"
    env = {"NEURA_HELD_OUT_STOP": str(stop)}
    leader = Mock(pid=1234)
    mask_before = signal.pthread_sigmask(signal.SIG_BLOCK, [])
    pass_fds = (17,)

    def spawn(*_args, **kwargs):
        assert kwargs["start_new_session"]
        assert kwargs["pass_fds"] == pass_fds
        assert signal.pthread_sigmask(signal.SIG_BLOCK, []) == mask_before
        stop.touch()
        signal.raise_signal(signal.SIGALRM)
        stop.unlink()
        return leader

    def handle_stop(_signum, _frame):
        PHASES.check_stop(env)

    with PHASES.alarm_handler(handle_stop), patch.object(PHASES.subprocess, "Popen", side_effect=spawn), \
            patch.object(PHASES.os, "killpg") as kill, patch.object(PHASES.os, "wait4") as wait:
        try:
            PHASES.timed(["synthetic-child"], env, pass_fds=pass_fds)
        except PHASES.StopRequested:
            pass
        else:
            raise AssertionError("STOP during spawn was lost")
        kill.assert_called_once_with(leader.pid, signal.SIGKILL)
        leader.wait.assert_called_once_with()
        wait.assert_not_called()
        assert signal.getsignal(signal.SIGALRM) is handle_stop
        assert signal.pthread_sigmask(signal.SIG_BLOCK, []) == mask_before


def check_descriptor_receipt(folder):
    child = folder / "receipt-child.py"
    child.write_text('''import json
import sys

descriptor = int(sys.argv[1].removeprefix("--report-fd="))
mode = sys.argv[2]
print("program output")
print("program diagnostics", file=sys.stderr)
with open(descriptor, "wb") as receipt:
    if mode == "malformed":
        receipt.write(b"{")
    elif mode == "not-an-object":
        receipt.write(b"[]")
    elif mode != "missing":
        receipt.write((json.dumps({"version": 1, "payload": "x" * 262144}) + "\\n").encode())
sys.exit(7 if mode == "failure" else 0)
''', encoding="utf-8")
    receipt_path = folder / "execution.json"
    expected = {"version": 1, "payload": "x" * 262144}
    expected_bytes = (json.dumps(expected) + "\n").encode()
    record = {"fixture": "synthetic"}

    def anonymous_descriptor(name):
        assert name == "neura-held-out-receipt"
        with tempfile.TemporaryFile() as stream:
            return os.dup(stream.fileno())

    def run_child(command, env, cwd, *, pass_fds):
        (descriptor,) = pass_fds
        assert command[2] == f"--report-fd={descriptor}"
        assert not receipt_path.exists()
        result = subprocess.run(command, env=env, cwd=cwd, pass_fds=pass_fds, capture_output=True, timeout=10)
        assert not receipt_path.exists()
        return record, result

    def run(mode, **options):
        receipt_path.unlink(missing_ok=True)
        return PHASES.companion_receipt([sys.executable, str(child), mode], {}, folder, receipt_path, **options)

    with patch.object(PHASES.os, "memfd_create", side_effect=anonymous_descriptor, create=True), \
            patch.object(PHASES, "timed", side_effect=run_child):
        for mode, status, options in (("success", 0, {}), ("failure", 7, {"allow_failure": True})):
            observed, process, receipt = run(mode, **options)
            assert observed == record and process.returncode == status and receipt == expected
            assert process.stdout == b"program output\n" and process.stderr == b"program diagnostics\n"
            assert receipt_path.read_bytes() == expected_bytes
        for mode, message, retained in (("failure", "companion failed", expected_bytes),
                                        ("missing", "companion failed", None),
                                        ("malformed", "invalid companion receipt", b"{"),
                                        ("not-an-object", "invalid companion receipt object", b"[]")):
            try:
                run(mode)
            except SystemExit as error:
                assert message in str(error)
            else:
                raise AssertionError(f"{mode} companion receipt was accepted")
            assert (receipt_path.read_bytes() if receipt_path.exists() else None) == retained
    with patch.object(PHASES.os, "memfd_create", side_effect=anonymous_descriptor, create=True), \
            patch.object(PHASES, "timed", side_effect=PHASES.StopRequested("synthetic STOP")):
        try:
            run("success")
        except PHASES.StopRequested:
            assert not receipt_path.exists()
        else:
            raise AssertionError("interrupted receipt was accepted")


def check_resume(folder):
    PHASES.output_directory(folder)
    receipt = folder / "slots.json"
    receipt.write_text("saved", encoding="utf-8")
    try:
        PHASES.output_directory(folder)
    except SystemExit as error:
        assert "fresh --out-dir" in str(error)
    else:
        raise AssertionError("existing output directory was accepted")
    assert receipt.read_text(encoding="utf-8") == "saved"


def main():
    with tempfile.TemporaryDirectory() as scratch:
        folder = Path(scratch)
        run_denied(folder / "denied")
        run_pair(folder / "pair")
        check_source_phase_boundary(folder)
        run_application(folder / "apps")
        check_partial_gate(folder)
        check_compile_overlap()
        check_tier_witness()
        check_suite_stderr()
        check_stop_during_spawn(folder)
        check_descriptor_receipt(folder)
        check_resume(folder / "occupied")
    print("held-out scheduling, qualification, tier diagnostics, receipt transport, output and STOP checks passed")


if __name__ == "__main__":
    main()

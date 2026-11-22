"""The evaluation-order gate of Html030.md: the trace of a factory with conditions and nested keyed lists.

A render is render-compatible (E1010), so a hole cannot print. What a factory call leaves is its view: the builder
records each slot value when its hole is evaluated, linking it in front of the earlier ones, and links each @for
item with its key in iteration order. Walking the views a call returns, oldest link first, therefore lists the
holes, branches and items in the order they were evaluated. (That a key is evaluated before its item's body is not
visible this way; neither can have an observable effect.) The walk reads the views through Std.Ui.Testing, a module
that exists only in this test's copy of the standard library. The trace must equal the hand-derived one for the
optimized and the unoptimized image, at every tier; a skipped branch leaves nothing; and a hole that traps ends the
call, so no view is returned and nothing after it runs.
"""

import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

NEURA = Path(sys.argv[1]).resolve()
SOURCE = Path(__file__).resolve().parents[3]


def copy_tools(bin_dir: Path) -> None:
    """Copies `neura` with the tools it starts and their shared libraries
    (docs/design/Products030.md), as the build tree lays them out."""
    source = NEURA.parent
    for tool in [NEURA, *sorted(source.glob("neura-*")), source / f"neurac{NEURA.suffix}"]:
        if tool.is_file():
            shutil.copy2(tool, bin_dir / tool.name)
    if (source / "lib/neura").is_dir():
        shutil.copytree(source / "lib/neura", bin_dir / "lib/neura")
ENV = dict(os.environ, NEURA_GPU="off")

SHIM = """let ui = import:Std.Ui;
let storage = import:Std.Ui.Internal.Storage;
let build = import:Std.Ui.Internal.Build;
let console = import:Std.Io.Console;

/// Test access to the slots of a view; present only in the layout of tests/apps/ui-ops/trace.py.
export let Testing = module (
    export let walk = func(view: ui.View, depth: string) {
        console.print_line(depth + "view " + view.fragment.to_string());
        for (let slot in build.slots_in_order(view)) {
            last at = depth + "  slot " + slot.index.to_string() + ": ";
            if (slot is storage.Payload<string>) {
                console.print_line(at + "text " + (slot as storage.Payload<string>).value);
            } else if (slot is storage.Payload<ui.View>) {
                console.print_line(at + "view");
                walk((slot as storage.Payload<ui.View>).value, depth + "    ");
            } else if (slot is storage.Payload<build.Items>) {
                console.print_line(at + "items");
                for (let item in build.items_in_order((slot as storage.Payload<build.Items>).value)) {
                    console.print_line(depth + "    key " + item.key);
                    walk(item.view, depth + "    ");
                }
            } else {
                console.print_line(at + "other");
            }
        }
    };
);
"""

PROGRAM = r"""
let console = import:Std.Io.Console;
let program = import:Std.Program;
let ui = import:Std.Ui;
let testing = import:Std.Ui.Testing;

let cell = html(label: string) { <em>{label}</em> };

let board = html(title: string, rows: string[], columns: int32[], show: bool, divisor: int32) {
    <section title={title}>
        <h2>{title}</h2>
        @if (show) {
            <table>
                <tbody>
                    @for (const row in rows; key: row) {
                        <tr>
                            <th>{row}</th>
                            @for (const column in columns; key: column) {
                                <td data-column={column}>{ row + ":" + column.to_string() }</td>
                            }
                        </tr>
                    }
                </tbody>
            </table>
        } @else {
            <p>{ "hidden " + title }</p>
        }
        <footer>{ cell("end") }{ 100 / divisor }{ rows[(divisor == 0) ? 9 : 0] }</footer>
    </section>
};

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        console.print_line("shown");
        testing.walk(board("T", ["a", "b"], [1, 2], true, 4), "");
        console.print_line("skipped");
        testing.walk(board("U", ["a"], [1], false, 5), "");
        console.print_line("trap");
        testing.walk(board("V", ["a"], [1], true, 0), "");
        console.print_line("unreachable");
        return program.ExitCode.Ok;
    };
);
"""

# Derived by hand from Html030.md, "Evaluation": attributes before children, document order, an @if condition
# once and only the chosen branch, an @for source once and then each item's key before its body. Fragment numbers
# are the descriptor's: the body is 0, then each control block's bodies in document order. The footer's three holes
# are a child view, a text, and a text whose index would trap in the call where the division traps first.
SHOWN = """\
view 0
  slot 0: text T
  slot 1: text T
  slot 2: view
    view 1
      slot 0: items
        key a
        view 2
          slot 0: text a
          slot 1: items
            key 1
            view 3
              slot 0: text 1
              slot 1: text a:1
            key 2
            view 3
              slot 0: text 2
              slot 1: text a:2
        key b
        view 2
          slot 0: text b
          slot 1: items
            key 1
            view 3
              slot 0: text 1
              slot 1: text b:1
            key 2
            view 3
              slot 0: text 2
              slot 1: text b:2
  slot 3: view
    view 0
      slot 0: text end
  slot 4: text 25
  slot 5: text a
"""
SKIPPED = """\
view 0
  slot 0: text U
  slot 1: text U
  slot 2: view
    view 4
      slot 0: text hidden U
  slot 3: view
    view 0
      slot 0: text end
  slot 4: text 20
  slot 5: text a
"""


def check(condition, message):
    if not condition:
        raise AssertionError(message)


with TemporaryDirectory() as temporary:
    root = Path(temporary)
    (root / "bin").mkdir()
    copy_tools(root / "bin")
    shutil.copytree(SOURCE / "stdlib", root / "stdlib")
    (root / "stdlib/std/ui/testing.nra").write_text(SHIM)
    (root / "main.nra").write_text(PROGRAM)
    neura = str(root / "bin/neura")
    builds = {"optimized": [], "unoptimized": ["--no-optimize"]}
    expected = "shown\n" + SHOWN + "skipped\n" + SKIPPED + "trap\n"
    for name, flags in builds.items():
        built = subprocess.run([neura, "build", *flags, "main.nra", "-o", f"{name}.nvr"], cwd=root, env=ENV,
                               capture_output=True, text=True)
        check(built.returncode == 0, built.stdout + built.stderr)
        for tier in ("--tier=0", "--tier=1", "--tier=2"):
            run = subprocess.run([neura, "run", f"{name}.nvr", tier], cwd=root, env=ENV, capture_output=True,
                                 text=True)
            # The division hole traps first; the index hole after it, which would trap differently, never runs.
            check(run.returncode != 0 and "source arithmetic failure" in run.stderr and
                  "main.nra:28:34" in run.stderr and "index" not in run.stderr,
                  f"{name} {tier}: the first trapping hole did not end the call\n{run.stderr}")
            check(run.stdout == expected, f"{name} {tier}: trace differs\n{run.stdout}")

print("evaluation trace matches at every tier, optimized and unoptimized")

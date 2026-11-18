"""Reconcile views in Neura and check the operations against an independent model of the DOM adapter.

Std.Ui.Internal.Reconcile is library-internal, so the test builds a layout of its own: a copy of the
`neura` executable beside a copy of the standard library that adds the public module Std.Ui.Testing,
which re-exports the reconciler. The Neura program prints each commit's operations as JSON and its
encoding as hexadecimal. This script applies the operations to a model tree with the phase rules of
hosts/web/README.md, compares the result with the expected tree, and decodes the bytes with the
commit encoding of docs/runtime/Providers.md (version 1) to check they carry the same operations.
"""

import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory

NEURA = Path(sys.argv[1]).resolve()
SOURCE = Path(__file__).resolve().parents[3]
ENV = dict(os.environ, NEURA_GPU="off")

SHIM = """let reconcile = import:Std.Ui.Internal.Reconcile;

/// Test access to the reconciler; present only in the layout of tests/apps/ui-ops/reconcile.py.
export let Testing = module (
    export let Reconciler = reconcile.Reconciler;
    export let Plan = reconcile.Plan;
);
"""

PROGRAM = r"""
let console = import:Std.Io.Console;
let program = import:Std.Program;
let ui = import:Std.Ui;
let testing = import:Std.Ui.Testing;
let.promote optional = import:Std.Core.Optional;

let clicks: int[list] = new();
let record = new func(event: ui.ClickEvent) { clicks.append(event.detail); };
let other = new func(event: ui.ClickEvent) { clicks.append(1000 + event.detail); };

let badge = html(label: string) { <em>{label}</em> };
let pair = html(first: ui.View, second: ui.View) {
    <div>{first}{second}</div>
};

let page = html(names: string[], title: Optional<string>, busy: bool, child: ui.View, wide: bool,
                draft: string, handler: func(ui.ClickEvent) -> void, cancel: bool) {
    <section title={title} aria-busy={busy}>
        <h1>Parts</h1>
        <ul>
            @for (const name in names; key: name) {
                <li>{name}</li>
            }
        </ul>
        {child}
        @if (wide) {
            <p var:--gap={ ui.length(2.0, "rem") }>Wide</p>
        } @else {
            <p>Narrow</p>
        }
        <input type="text" prop:value={draft} disabled={busy}>
        @if (cancel) {
            <button type="button" on:click.prevent_default={handler}>Go</button>
        } @else {
            <button type="button" on:click={handler}>Go</button>
        }
    </section>
};

let rows = html(count: int) {
    <ol>
        @for (const i in {0 .. count}; key: i) {
            <li>{i}</li>
        }
    </ol>
};

let show = func(r: testing.Reconciler, label: string, view: ui.View) {
    last plan = r.prepare(view);
    console.print_line("commit " + label);
    if (plan.diagnostic != nil) {
        console.print_line("invalid " + plan.diagnostic.message + " slot=" + plan.diagnostic.slot.to_string() +
                           " index=" + plan.diagnostic.index.to_string() + " key=" + plan.diagnostic.key);
        return;
    }
    for (let operation in plan.operations) {
        console.print_line("op " + operation.to_string());
    }
    let hex = "";
    for (let byte in plan.encode()) {
        last digits = "0123456789abcdef";
        hex = hex + digits.slice((byte >> 4) as int, ((byte >> 4) as int) + 1) +
              digits.slice((byte & 15) as int, ((byte & 15) as int) + 1);
    }
    console.print_line("bytes " + hex);
    r.accept(plan);
};

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        last r = new testing.Reconciler();
        show(r, "mount", page(["a", "b", "c"], Optional<string>.Some("Stock"), false, badge("new"), true, "x",
                              record, false));
        console.print_line("deliver " + r.deliver<ui.ClickEvent>(1, new ui.ClickEvent(0, 0.0, 0.0,
            new ui.Modifiers(false, false, false, false), 7)).to_string());
        show(r, "update", page(["c", "d", "a"], Optional<string>.None, true, badge("old"), false, "y",
                               other, true));
        console.print_line("deliver " + r.deliver<ui.ClickEvent>(1, new ui.ClickEvent(0, 0.0, 0.0,
            new ui.Modifiers(false, false, false, false), 9)).to_string());
        console.print_line("deliver " + r.deliver<ui.ClickEvent>(2, new ui.ClickEvent(0, 0.0, 0.0,
            new ui.Modifiers(false, false, false, false), 8)).to_string());
        console.print_line("wrong record " + r.deliver<ui.InputEvent>(2, new ui.InputEvent("", false, false)).to_string());
        console.print_line("handlers " + r.handler_count().to_string());
        for (let value in clicks) {
            console.print_line("clicked " + value.to_string());
        }
        last shared = badge("twice");
        show(r, "reused", pair(shared, shared));
        show(r, "duplicate", page(["a", "b", "a", "b"], Optional<string>.None, false, badge("d"), true, "",
                                  record, false));
        show(r, "same", page(["c", "d", "a"], Optional<string>.None, true, badge("old"), false, "y", other, true));
        last mounted = page(["z"], Optional<string>.None, false, badge("m"), true, "", record, false);
        show(r, "replace", mounted);
        show(r, "remount", mounted);
        last list = new testing.Reconciler();
        show(list, "limit", rows(10001));
        show(list, "rows", rows(3));
        list.reset();
        show(list, "after reset", rows(2));
        return program.ExitCode.Ok;
    };
);
"""

OPS = ["remove", "element", "text", "move", "setText", "attribute", "variable", "handler", "property"]


def decode(data):
    """The commit encoding, version 1, read independently of the Neura encoder."""
    at = 0

    def take(fmt):
        nonlocal at
        values = struct.unpack_from("<" + fmt, data, at)
        at += struct.calcsize("<" + fmt)
        return values[0]

    def text():
        nonlocal at
        size = take("I")
        value = data[at:at + size].decode("utf-8")
        at += size
        return value

    assert take("B") == 1, "commit encoding version"
    result = []
    for _ in range(take("I")):
        op = OPS[take("B") - 1]
        entry = {"op": op, "id": take("I")}
        if op in ("element", "text", "move"):
            entry["parent"] = take("I")
            before = take("I")
            entry["before"] = None if before == 0 else before
            if op == "element":
                entry["svg"] = take("B") == 1
                entry["name"] = text()
            elif op == "text":
                entry["value"] = text()
        elif op == "setText":
            entry["value"] = text()
        elif op == "attribute":
            entry["name"] = text()
            entry["value"] = text() if take("B") else None
        elif op == "variable":
            entry["name"] = text()
            entry["value"] = {"number": take("d"), "unit": text()} if take("B") else None
        elif op == "handler":
            entry["event"] = text()
            handler = take("Q")
            entry["handler"] = None if handler == 0 else handler
            flags = take("B")
            entry["preventDefault"] = bool(flags & 1)
            entry["stopPropagation"] = bool(flags & 2)
        elif op == "property":
            entry["name"] = text()
            entry["value"] = take("B") == 1 if take("B") == 1 else text()
        result.append(entry)
    assert at == len(data), "trailing bytes"
    return result


class Model:
    """The decoded operations applied as hosts/web/README.md describes them."""

    def __init__(self):
        self.nodes = {0: {"tag": None, "children": []}}
        self.parent = {}

    def detach(self, node):
        if node in self.parent:
            self.nodes[self.parent[node]]["children"].remove(node)
            del self.parent[node]

    def insert(self, node, parent, before):
        self.detach(node)
        children = self.nodes[parent]["children"]
        children.insert(len(children) if before is None else children.index(before), node)
        self.parent[node] = parent

    def release(self, node):
        for child in list(self.nodes[node].get("children", [])):
            self.release(child)
        del self.nodes[node]
        self.parent.pop(node, None)

    def apply(self, operations):
        phase = 0
        order = {"remove": 0, "element": 1, "text": 1, "move": 1, "setText": 2, "attribute": 2, "variable": 2,
                 "handler": 3, "property": 4}
        for op in operations:
            assert order[op["op"]] >= phase, f"phase order broken at {op}"
            phase = order[op["op"]]
            kind, node = op["op"], op["id"]
            if kind == "remove":
                self.detach(node)
                self.release(node)
            elif kind == "element":
                assert node not in self.nodes
                self.nodes[node] = {"tag": op["name"], "attrs": {}, "vars": {}, "props": {}, "on": {},
                                    "children": []}
                self.insert(node, op["parent"], op["before"])
            elif kind == "text":
                assert node not in self.nodes
                self.nodes[node] = {"text": op["value"]}
                self.insert(node, op["parent"], op["before"])
            elif kind == "move":
                self.insert(node, op["parent"], op["before"])
            elif kind == "setText":
                self.nodes[node]["text"] = op["value"]
            elif kind == "attribute":
                if op["value"] is None:
                    del self.nodes[node]["attrs"][op["name"]]
                else:
                    self.nodes[node]["attrs"][op["name"]] = op["value"]
            elif kind == "variable":
                if op["value"] is None:
                    del self.nodes[node]["vars"][op["name"]]
                else:
                    self.nodes[node]["vars"][op["name"]] = op["value"]
            elif kind == "handler":
                if op["handler"] is None:
                    del self.nodes[node]["on"][op["event"]]
                else:
                    flags = ("p" if op["preventDefault"] else "") + ("s" if op["stopPropagation"] else "")
                    self.nodes[node]["on"][op["event"]] = f"{op['handler']}{flags}"
            elif kind == "property":
                self.nodes[node]["props"][op["name"]] = op["value"]

    def html(self, node=0):
        entry = self.nodes[node]
        if "text" in entry:
            return entry["text"]
        inner = "".join(self.html(child) for child in entry["children"])
        if entry["tag"] is None:
            return inner
        parts = [entry["tag"]]
        parts += [f'{name}="{value}"' if value else name for name, value in entry["attrs"].items()]
        parts += [f"--{name[2:]}={value['number']:g}{value['unit']}" for name, value in entry["vars"].items()]
        parts += [f"on:{event}={handler}" for event, handler in entry["on"].items()]
        parts += [f"prop:{name}={json.dumps(value)}" for name, value in entry["props"].items()]
        return f"<{' '.join(parts)}>{inner}</{entry['tag']}>"


MOUNT = ('<section title="Stock" aria-busy="false"><h1>Parts</h1><ul><li>a</li><li>b</li><li>c</li></ul>'
         ' <em>new</em> <p --gap=2rem>Wide</p><input type="text" prop:value="x"></input>'
         '<button type="button" on:click=1>Go</button></section>')
UPDATE = ('<section aria-busy="true"><h1>Parts</h1><ul><li>c</li><li>d</li><li>a</li></ul>'
          ' <em>old</em> <p>Narrow</p><input type="text" disabled prop:value="y"></input>'
          '<button type="button" on:click=2p>Go</button></section>')


def check(condition, message):
    if not condition:
        raise AssertionError(message)


with TemporaryDirectory() as temporary:
    root = Path(temporary)
    (root / "bin").mkdir()
    shutil.copy2(NEURA, root / "bin/neura")
    shutil.copytree(SOURCE / "stdlib", root / "stdlib")
    (root / "stdlib/std/ui/testing.nra").write_text(SHIM)
    (root / "main.nra").write_text(PROGRAM)
    neura = str(root / "bin/neura")
    built = subprocess.run([neura, "build", "main.nra", "-o", "main.nvr"], cwd=root, env=ENV,
                           capture_output=True, text=True)
    check(built.returncode == 0, built.stdout + built.stderr)
    runs = [subprocess.run([neura, "run", "main.nvr", *tier], cwd=root, env=ENV, capture_output=True, text=True)
            for tier in ([], ["--tier=0"])]
    for run in runs:
        check(run.returncode == 0, run.stdout + run.stderr)
    check(runs[0].stdout == runs[1].stdout, "tier 0 and the default tier disagree")
    lines = runs[0].stdout.splitlines()

    commits, current, extras = {}, None, []
    for line in lines:
        kind, _, rest = line.partition(" ")
        if kind == "commit":
            current = rest
            commits[current] = {"ops": [], "bytes": None, "invalid": None}
        elif kind == "op":
            commits[current]["ops"].append(json.loads(rest))
        elif kind == "bytes":
            commits[current]["bytes"] = bytes.fromhex(rest)
        elif kind == "invalid":
            commits[current]["invalid"] = rest
        else:
            extras.append(line)

    for name, commit in commits.items():
        if commit["bytes"] is not None:
            check(decode(commit["bytes"]) == commit["ops"], f"{name}: the encoding differs from the operations")

    model = Model()
    model.apply(commits["mount"]["ops"])
    check(model.html() == MOUNT, f"mount gave {model.html()}")
    ids_before = {node: entry.get("tag") for node, entry in model.nodes.items()}
    model.apply(commits["update"]["ops"])
    check(model.html() == UPDATE, f"update gave {model.html()}")
    # Keyed items, the heading and the section keep their nodes; the list reorders with one move.
    update_ops = commits["update"]["ops"]
    check(sum(1 for op in update_ops if op["op"] == "move") == 1, "a reorder of three keyed items takes one move")
    check(not any(op["op"] == "remove" and ids_before.get(op["id"]) in ("section", "h1", "ul") for op in update_ops),
          "a kept element was replaced")
    check(any(op["op"] == "setText" and op["value"] == "old" for op in update_ops), "a text hole did not update")

    check(commits["reused"]["invalid"].startswith("a view appears once"), commits["reused"])
    duplicate = commits["duplicate"]["invalid"]
    # The second item of the first equal pair in iteration order, in the slot of the @for.
    check("key 'a'" in duplicate and "index=2 key=a" in duplicate, duplicate)
    # Committing equal content changes nothing but the controlled property.
    same_ops = commits["same"]["ops"]
    check([op["op"] for op in same_ops] == ["property"], f"an equal view issued {same_ops}")
    model.apply(same_ops)
    check(model.html() == UPDATE, "an equal commit changed the tree")
    model.apply(commits["replace"]["ops"])
    check(commits["remount"]["invalid"].startswith("a view appears once"), "recommitting the mounted view")
    check(commits["limit"]["invalid"].startswith("U-4") and "index=10000" in commits["limit"]["invalid"],
          commits["limit"])
    rows = Model()
    rows.apply(commits["rows"]["ops"])
    check(rows.html() == "<ol><li>0</li><li>1</li><li>2</li></ol>", rows.html())
    fresh = Model()
    fresh.apply(commits["after reset"]["ops"])
    check(fresh.html() == "<ol><li>0</li><li>1</li></ol>", "a reset root mounts from scratch")
    check(min(op["id"] for op in commits["after reset"]["ops"]) > max(rows.nodes), "node ids are not reused")

    # Handler 1 left with the replaced button; handler 2 holds the new callable.
    check(extras == ["deliver true", "deliver false", "deliver true", "wrong record false", "handlers 1",
                     "clicked 7", "clicked 1008"],
          f"delivery {extras}")

print("reconciliation checks passed")

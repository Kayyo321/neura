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
# With --update, rewrite the reconciler entries of batches.json instead of checking them.
UPDATE = sys.argv[2:] == ["--update"]
SOURCE = Path(__file__).resolve().parents[3]


def copy_tools(bin_dir: Path) -> None:
    """Copies `neura` with the tools it starts and their shared libraries
    (docs/design/Products030.md), as the build tree lays them out."""
    source = NEURA.parent
    for tool in [NEURA, *sorted(source.glob("neura-*")), source / "neurac"]:
        if tool.is_file():
            shutil.copy2(tool, bin_dir / tool.name)
    if (source / "lib/neura").is_dir():
        shutil.copytree(source / "lib/neura", bin_dir / "lib/neura")
BATCHES = Path(__file__).resolve().parent / "batches.json"
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

let note = html(text: string) { <small>{text}</small> };

let items = html(names: string[], tail: string) {
    <ul>
        @for (const name in names; key: name) {
            <li>{name}</li>
        }
        <li>{tail}</li>
    </ul>
};

let group = html(title: string, members: string[], open: bool) {
    <section>
        <h2>{title}</h2>
        @if (open) {
            <ol>
                @for (const member in members; key: member) {
                    <li>
                        {member}
                        @if (member == title) {
                            <strong>lead</strong>
                        }
                    </li>
                }
            </ol>
        } @else {
            <p>closed</p>
        }
    </section>
};

let groups = html(names: string[], open: bool) {
    @for (const name in names; key: name) {
        {group(name, ["a", name, "z"], open)}
    }
    <footer>end</footer>
};

let typed = new func(event: ui.InputEvent) {};

let controls = html(checked: bool, text: string, open: bool, choice: string, accent: uint32, ratio: float64,
                    link: string, stop: bool) {
    <form>
        <input type="checkbox" prop:checked={checked} prop:indeterminate={!checked}>
        <textarea prop:value={text} on:input={typed}></textarea>
        <details prop:open={open}><summary>More</summary><p>Body</p></details>
        <select prop:value={choice}><option value="a">A</option><option value="b">B</option></select>
        <div var:--accent={ ui.color(accent) } var:--ratio={ ui.percent(ratio) } var:--scale={ ui.number(1.5) }>
            Styled
        </div>
        <a href={ ui.url(link) }>Link</a>
        @if (stop) {
            <button type="button" on:click.stop_propagation={record}>Stop</button>
        } @else {
            <button type="button" on:click={record}>Go</button>
        }
    </form>
};

let icon = html(label: string) {
    <figure>
        <svg viewBox="0 0 4 4" aria-hidden="true"><path d="M 0 0 L 4 4"/></svg>
        <figcaption>{label}</figcaption>
    </figure>
};

let bulk = html(lists: int, size: int, extra: int) {
    @for (const l in {0 .. lists}; key: l) {
        <ol>
            @for (const i in {0 .. size}; key: i) {
                <li></li>
            }
        </ol>
    }
    @for (const e in {0 .. extra}; key: e) {
        <hr>
    }
};

let sized = html(text: string, title: string, link: ui.Url) {
    <p title={title}><a href={link}>{text}</a></p>
};

// Validation only: the outcome, or the number of operations.
let measure = func(r: testing.Reconciler, label: string, view: ui.View) {
    last plan = r.prepare(view);
    console.print_line("commit " + label);
    if (plan.diagnostic != nil) {
        console.print_line("invalid " + plan.diagnostic.message + " slot=" + plan.diagnostic.slot.to_string() +
                           " index=" + plan.diagnostic.index.to_string() + " key=" + plan.diagnostic.key);
        return;
    }
    console.print_line("count " + plan.operations.length.to_string());
    r.accept(plan);
};

let scenario = func(name: string) {
    console.print_line("scenario " + name);
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
        scenario("reconciler: mount, keyed update, equal commit and replacement");
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
        scenario("reconciler: keyed items before a static sibling");
        last keyed = new testing.Reconciler();
        show(keyed, "keyed 1", items(["a", "b", "c", "d", "e"], "end"));
        show(keyed, "keyed 2", items(["e", "d", "c", "b", "a"], "end"));
        show(keyed, "keyed 3", items(["x", "a", "y", "c", "z"], "tail"));
        show(keyed, "keyed 4", items([], "tail"));
        show(keyed, "keyed 5", items(["p", "q"], "end"));
        scenario("reconciler: nested blocks and child views at the root");
        last nested = new testing.Reconciler();
        show(nested, "nested 1", groups(["g1", "g2"], true));
        show(nested, "nested 2", groups(["g2", "g1", "g3"], false));
        show(nested, "nested 3", groups(["g3", "g1"], true));
        show(nested, "nested 4", groups([], true));
        scenario("reconciler: child views change template");
        last children = new testing.Reconciler();
        show(children, "children 1", pair(badge("x"), note("y")));
        show(children, "children 2", pair(note("y"), badge("x")));
        show(children, "children 3", pair(note("w"), note("v")));
        scenario("reconciler: properties, custom properties, URLs and handlers");
        last forms = new testing.Reconciler();
        show(forms, "controls 1", controls(true, "hello", true, "b", 16746496 as uint32, 25.0, "/parts/7", false));
        show(forms, "controls 2", controls(false, "hi", false, "a", 255 as uint32, 12.5, "https://example.org/x",
                                           true));
        show(forms, "controls 3", controls(false, "hi", false, "a", 255 as uint32, 12.5, "https://example.org/x",
                                           true));
        scenario("reconciler: static SVG with a dynamic caption");
        last icons = new testing.Reconciler();
        show(icons, "icon 1", icon("first"));
        show(icons, "icon 2", icon("second"));
        scenario("-");
        last list = new testing.Reconciler();
        show(list, "limit", rows(10001));
        show(list, "rows", rows(3));
        list.reset();
        show(list, "after reset", rows(2));

        // U-5 counts every element and text node of a root: ten lists of one <ol> and 9999 items make 100000.
        last big = new testing.Reconciler();
        measure(big, "nodes at the limit", bulk(10, 9999, 0));
        measure(big, "nodes over the limit", bulk(10, 9999, 1));
        // U-6: a text, attribute or URL value of 1 MiB is accepted, one byte more is refused.
        let mebibyte = "x";
        for (let i in {0 .. 20}) {
            mebibyte = mebibyte + mebibyte;
        }
        last values = new testing.Reconciler();
        measure(values, "values at the limit", sized(mebibyte, mebibyte, ui.url(mebibyte)));
        measure(values, "text over the limit", sized(mebibyte + "y", "t", ui.url("u")));
        measure(values, "attribute over the limit", sized("t", mebibyte + "y", ui.url("u")));
        measure(values, "url over the limit", sized("t", "t", ui.url(mebibyte + "y")));

        // ViewReused across roots: a view mounted in one root is refused by another until the first lets it go.
        last root_a = new testing.Reconciler();
        last root_b = new testing.Reconciler();
        last moved = badge("moved");
        measure(root_a, "root a mounts", moved);
        measure(root_b, "root b takes a view mounted in a", moved);
        measure(root_a, "root a mounts another view", badge("other"));
        measure(root_b, "root b takes the view a released", moved);
        last refused_once = badge("nested");
        measure(root_a, "root a refuses a tree", pair(refused_once, refused_once));
        measure(root_b, "root b takes a view a only refused", refused_once);
        last kept = badge("kept");
        measure(root_a, "root a mounts kept", kept);
        root_a.reset();
        measure(root_b, "root b takes a view a reset", kept);
        measure(root_a, "root a takes the view b mounted", kept);
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
                self.nodes[node] = {"tag": op["name"], "svg": op["svg"], "attrs": {}, "vars": {}, "props": {},
                                    "on": {}, "children": []}
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
                    self.nodes[node]["on"][op["event"]] = (op["handler"], op["preventDefault"],
                                                           op["stopPropagation"])
            elif kind == "property":
                self.nodes[node]["props"][op["name"]] = op["value"]

    def number(self, value):
        """A number as JavaScript's String(number) writes it, for the values the corpus uses."""
        return str(int(value)) if float(value).is_integer() else repr(float(value))

    def canonical(self, node):
        """The node in the form of canonical() in dom-test.mjs."""
        entry = self.nodes[node]
        if "text" in entry:
            return entry["text"]
        result = {"tag": entry["tag"]}
        svg = entry.get("svg", False)
        if svg:
            result["svg"] = True
        attrs = dict(entry["attrs"])
        for name in ("open",):
            if entry["props"].get(name) is True:
                attrs[name] = ""
            elif entry["props"].get(name) is False:
                attrs.pop(name, None)
        if attrs:
            result["attrs"] = {name: attrs[name] for name in sorted(attrs)}
        if entry["vars"]:
            result["vars"] = {
                name: ("#%06x" % int(value["number"]) if value["unit"] == "rgb"
                       else self.number(value["number"]) + value["unit"])
                for name, value in sorted(entry["vars"].items())}
        props = {name: value for name, value in sorted(entry["props"].items()) if name != "open"}
        if props:
            result["props"] = props
        if entry["children"]:
            result["children"] = [self.canonical(child) for child in entry["children"]]
        return result

    def walk(self, node=0, path=()):
        for index, child in enumerate(self.nodes[node].get("children", [])):
            yield child, [*path, index]
            yield from self.walk(child, (*path, index))

    def fixture(self):
        """The expected tree, node identities and handlers after a commit, for dom-test.mjs."""
        ids, events = [], []
        for node, path in self.walk():
            ids.append([node, path])
            for event, handler in sorted(self.nodes[node].get("on", {}).items()):
                events.append({"path": path, "event": event, "node": node, "handler": handler[0],
                               "preventDefault": handler[1], "stopPropagation": handler[2]})
        return {"tree": [self.canonical(child) for child in self.nodes[0]["children"]], "ids": ids,
                "events": events}

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
        parts += [f"on:{event}={handler}{'p' if prevent else ''}{'s' if stop else ''}"
                  for event, (handler, prevent, stop) in entry["on"].items()]
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
    copy_tools(root / "bin")
    shutil.copytree(SOURCE / "stdlib", root / "stdlib")
    (root / "stdlib/std/ui/testing.nra").write_text(SHIM)
    (root / "main.nra").write_text(PROGRAM)
    neura = str(root / "bin/neura")
    built = subprocess.run([neura, "build", "main.nra", "-o", "main.nvr"], cwd=root, env=ENV,
                           capture_output=True, text=True)
    check(built.returncode == 0, built.stdout + built.stderr)
    # The U-5 commits build, validate and diff 100000 nodes, which takes more than the default step limit.
    runs = [subprocess.run([neura, "run", "main.nvr", "--max-steps", "4000000000", *tier], cwd=root, env=ENV,
                           capture_output=True, text=True)
            for tier in ([], ["--tier=0"])]
    for run in runs:
        check(run.returncode == 0, run.stdout + run.stderr)
    check(runs[0].stdout == runs[1].stdout, "tier 0 and the default tier disagree")
    lines = runs[0].stdout.splitlines()

    commits, current, extras, scenarios = {}, None, [], {}
    scenario = None
    for line in lines:
        kind, _, rest = line.partition(" ")
        if kind == "scenario":
            scenario = None if rest == "-" else scenarios.setdefault(rest, [])
        elif kind == "commit":
            current = rest
            commits[current] = {"ops": [], "bytes": None, "invalid": None}
            if scenario is not None:
                scenario.append(current)
        elif kind == "op":
            commits[current]["ops"].append(json.loads(rest))
        elif kind == "bytes":
            commits[current]["bytes"] = bytes.fromhex(rest)
        elif kind == "invalid":
            commits[current]["invalid"] = rest
        elif kind == "count":
            commits[current]["count"] = int(rest)
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

    def outcome(label):
        commit = commits[label]
        return commit["invalid"] if commit["invalid"] is not None else None

    check(commits["nodes at the limit"]["invalid"] is None, commits["nodes at the limit"])
    check(outcome("nodes over the limit") == "U-5: a root holds at most 100000 nodes slot=0 index=-1 key=",
          outcome("nodes over the limit"))
    check(outcome("values at the limit") is None, outcome("values at the limit"))
    for label, slot in (("text over the limit", 2), ("attribute over the limit", 0), ("url over the limit", 1)):
        check(outcome(label) == f"U-6: a text or attribute value holds at most 1 MiB slot={slot} index=-1 key=",
              f"{label}: {outcome(label)}")
    reused = "a view appears once in one tree, in one root slot=0 index=-1 key="
    check([outcome(label) for label in ("root a mounts", "root b takes a view mounted in a",
                                        "root a mounts another view", "root b takes the view a released",
                                        "root a refuses a tree", "root b takes a view a only refused",
                                        "root a mounts kept", "root b takes a view a reset",
                                        "root a takes the view b mounted")] ==
          [None, reused, None, None, reused, None, None, None, reused], "views across roots")

    # The browser corpus: each scenario's accepted commits, with the tree the model builds from them. The
    # adapter in headless Chromium and Firefox must build the same tree (tests/apps/ui-ops/browser.py).
    generated = []
    for name, labels in scenarios.items():
        model, entries = Model(), []
        for label in labels:
            commit = commits[label]
            if commit["invalid"] is not None:
                continue
            model.apply(commit["ops"])
            entries.append({"operations": commit["ops"], **model.fixture()})
        generated.append({"name": name, "source": "tests/apps/ui-ops/reconcile.py", "commits": entries})
    corpus = json.loads(BATCHES.read_text())
    kept = [sample for sample in corpus if sample.get("source") != "tests/apps/ui-ops/reconcile.py"]
    if UPDATE:
        # Hand-written entries come first and keep their text; generated ones follow them.
        text, decoder, at, ends = BATCHES.read_text(), json.JSONDecoder(), 0, []
        at = text.index("[") + 1
        for sample in corpus:
            while text[at] in " \t\r\n,":
                at += 1
            _, at = decoder.raw_decode(text, at)
            ends.append(at)
        check(corpus[:len(kept)] == kept, "generated entries must follow the hand-written ones")
        prefix = text[:ends[len(kept) - 1]] if kept else "["
        lines = []
        for sample in generated:
            commit_lines = []
            for commit in sample["commits"]:
                fields = [f'        "operations": [\n' + ",\n".join(
                    "          " + json.dumps(op, ensure_ascii=False, separators=(",", ":"))
                    for op in commit["operations"]) + "\n        ]"]
                for field in ("tree", "ids", "events"):
                    if field in commit:
                        fields.append(f'        "{field}": ' + json.dumps(commit[field], ensure_ascii=False,
                                                                       separators=(",", ":")))
                commit_lines.append("      {\n" + ",\n".join(fields) + "\n      }")
            head = f'    "name": {json.dumps(sample["name"])},\n'
            if "source" in sample:
                head += f'    "source": {json.dumps(sample["source"])},\n'
            lines.append("  {\n" + head + '    "commits": [\n' + ",\n".join(commit_lines) + "\n    ]\n  }")
        BATCHES.write_text(prefix + (",\n" if kept else "\n") + ",\n".join(lines) + "\n]\n")
        rewritten = json.loads(BATCHES.read_text())
        check(rewritten == kept + generated, "batches.json did not round-trip")
    else:
        check([sample for sample in corpus if sample.get("source") == "tests/apps/ui-ops/reconcile.py"] == generated,
              "batches.json is out of date: run reconcile.py <neura> --update")

print("reconciliation checks passed")

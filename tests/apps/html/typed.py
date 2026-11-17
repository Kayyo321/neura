"""Exercise typed templates through the source, NeuraIR, and image entry points."""

import json
import os
from pathlib import Path
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory


NEURA = Path(sys.argv[1]).resolve()
ENV = dict(os.environ, NEURA_GPU="off")

PREFIX = """\
let ui = import:Std.Ui;
let optional = import:Std.Core.Optional;

let child = html(title: string) { <span>{title}</span> };

export let Pages = module (
    export let show = html(flag: bool, parts: int32[], title: string,
                           receive: func(ui.ClickEvent) -> void)
"""
FRAGMENT = """
        <section title={optional.Optional<string>.Some(title)} hidden={flag} aria-hidden={flag}>
            <button on:click.prevent_default={receive}>Café</button>
            <a href={ui.url("/items")} var:--space={ui.length(2.0, "px")}>Items</a>
            {child("ready")}
            @if (flag) {
                <p>{1 / parts[0]}</p>
            } @else {
                <ul>
                    @for (let part: int32 in parts; key: part) {
                        <li data-part={part}>{part}</li>
                    }
                </ul>
            }
        </section>
    """
MAIN = """\
let ui = import:Std.Ui;
let program = import:Std.Program;
let pages = import:"./pages.nir".Pages;

let App = module:program.Entry (
    let receive = func(event: ui.ClickEvent) { };

    export let.override main = func(args: string[]) -> program.ExitCode {
        let parts: int32[] = [0, -1, 2];
        last rendered = pages.show(false, parts, "title", receive);
        return rendered == nil ? program.ExitCode.Failure : program.ExitCode.Ok;
    };
);
"""
OVERLOAD = """\
let program = import:Std.Program;
let view = html(value: int32) { <p>{value}</p> }
    or html(value: string) { <p>{value}</p> };

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        last numeric = view(42);
        last text = view("text");
        return numeric == nil || text == nil ? program.ExitCode.Failure : program.ExitCode.Ok;
    };
);
"""


def neura(root, *arguments, error=None):
    process = subprocess.run([NEURA, *arguments], cwd=root, env=ENV, capture_output=True, text=True, timeout=60)
    if error:
        if process.returncode != 1 or error not in process.stderr:
            raise AssertionError(f"{arguments}: expected {error}\n{process.stderr}")
        return process.stderr
    if process.returncode != 0:
        raise AssertionError(f"{arguments}: status {process.returncode}\n{process.stderr}")
    return process.stdout


def sections(path):
    data = path.read_bytes()
    if data[:4] != b"NEUR":
        raise AssertionError(f"{path} does not contain an NVR image")
    count = struct.unpack_from("<I", data, 12)[0]
    result = {}
    for index in range(count):
        name, _, offset, size = struct.unpack_from("<4sIQQ", data, 24 + index * 24)
        result[name] = data[offset:offset + size]
    return result


def check_descriptor(dump):
    raw = []
    for line in dump.splitlines():
        line = line.lstrip()
        if line.startswith(r'string "{\"encoding\":1,'):
            raw.append(json.loads(line.removeprefix("string ").split(" : string at ", 1)[0]))
    if len(raw) != 2:
        raise AssertionError("expected one descriptor for each factory")
    for descriptor in raw:
        parsed = json.loads(descriptor)
        if descriptor != json.dumps(parsed, ensure_ascii=False, sort_keys=True, separators=(",", ":")):
            raise AssertionError("the descriptor is not canonical JSON")
    fragments = max((json.loads(item)["fragments"] for item in raw), key=len)
    if len(fragments) != 4:
        raise AssertionError("expected the root, two branches, and the loop body")
    slots = [(slot["kind"], slot.get("name")) for slot in fragments[0]["slots"]]
    if slots != [("attribute", "title"), ("flag", "hidden"), ("attribute", "aria-hidden"),
                 ("handler", "click"), ("attribute", "href"), ("custom-property", "--space"),
                 ("child", None), ("block", None)]:
        raise AssertionError(f"unexpected slot kinds or document order: {slots}")


def build_routes(root, prefix):
    (root / "app.nra").write_text(MAIN.replace('import:"./pages.nir".Pages', "import:Pages"), encoding="utf-8")
    neura(root, "build", "app.nra", "pages.nra", "-o", f"{prefix}-source.nvr")
    neura(root, "build", "--no-source-map", "app.nra", "pages.nra", "-o", f"{prefix}-stripped.nvr")
    neura(root, "emit-ir", "app.nra", "pages.nra", "-o", "program.nir")
    dump = neura(root, "emit-ir", "--no-optimize", "program.nir")
    for expected in ("@ui.factory(", r'{\"encoding\":1,\"fragments\":', "branch", "loop"):
        if expected not in dump:
            raise AssertionError(f"IR dump missing {expected}")
    check_descriptor(dump)
    neura(root, "build", "program.nir", "-o", f"{prefix}-rebuilt.nvr")
    if (root / f"{prefix}-source.nvr").read_bytes() != (root / f"{prefix}-rebuilt.nvr").read_bytes():
        raise AssertionError("source and rebuilt NIR differ")
    neura(root, "emit-ir", "pages.nra", "-o", "pages.nir")
    (root / "app.nra").write_text(MAIN, encoding="utf-8")
    neura(root, "build", "app.nra", "-o", f"{prefix}-linked.nvr")
    return dump


def compare_views(root):
    pages = root / "pages.nra"
    pages.write_text(PREFIX + "{" + FRAGMENT + "};\n);\n", encoding="utf-8")
    inline = build_routes(root, "inline")
    # The bytes differ in line endings and an ignored BOM. Both source maps
    # must still point back to the holes in the source actually read.
    (root / "view.html").write_bytes(b"\xef\xbb\xbf" + FRAGMENT.replace("\n", "\r\n").encode())
    pages.write_text(PREFIX + 'from "./view.html";\n);\n', encoding="utf-8")
    external = build_routes(root, "external")
    if "view.html" in inline or "view.html" not in external:
        raise AssertionError("template spans are missing from the external source in NeuraIR")
    for route in ("source", "rebuilt", "linked"):
        left = sections(root / f"inline-{route}.nvr")
        right = sections(root / f"external-{route}.nvr")
        if b"SMAP" not in left or b"SMAP" not in right or left.pop(b"SMAP") == right.pop(b"SMAP"):
            raise AssertionError(f"{route} omitted the inline/external source-map difference")
        if left != right:
            raise AssertionError(f"{route} inline/external difference outside the source map")
    left = (root / "inline-stripped.nvr").read_bytes()
    right = (root / "external-stripped.nvr").read_bytes()
    if left != right:
        raise AssertionError("stripped inline and external image bytes differ")
    # Deployed construction consumes only the executable image.
    (root / "view.html").unlink()
    for name in ("inline-stripped", "external-stripped", "external-linked"):
        neura(root, "run", "--tier=0", f"{name}.nvr")
        neura(root, "run", "--jit=off", f"{name}.nvr")


def file_boundaries(root):
    source = root / "bad.nra"
    source.write_text('let view = html() from "./bad.html";\n', encoding="utf-8")
    path = root / "bad.html"
    path.write_bytes(b"\xff")
    message = neura(root, "check", "--error-format=short", source.name, error="E1011")
    if "bad.nra:1:24" not in message:
        raise AssertionError(f"expected the path's span\n{message}")
    path.write_bytes(b" " * (1024 * 1024 + 1))
    neura(root, "check", "--error-format=short", source.name, error="E1011")
    path.write_text("<p>{false}</p>", encoding="utf-8")
    message = neura(root, "check", "--error-format=short", source.name, error="E1004")
    if "bad.html:1:4" not in message:
        raise AssertionError(f"expected the hole's span in external source\n{message}")
    path.unlink()
    outside = root.parent / "outside.html"
    outside.write_text("<p>outside</p>", encoding="utf-8")
    if os.name != "nt":
        path.symlink_to(outside)
        neura(root, "check", "--error-format=short", source.name, error="E1011")


def library_values(root):
    source = root / "values.nra"
    program = """\
let ui = import:Std.Ui;
let program = import:Std.Program;

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {{
        {body}
        return program.ExitCode.Ok;
    }};
);
"""
    source.write_text(program.format(body="""\
last urls = [ui.url("./item"), ui.url("HTTPS://example.org/item"), ui.url("http://example.org/item"),
                        ui.url("mailto:hello@example.org"), ui.url("tel:+15550100")];
        last numbers = [ui.length(2.0, "rem"), ui.color(10682368), ui.number(-0.5), ui.percent(100.0)];
        if (urls[1] == nil || numbers[3] == nil) {
            return program.ExitCode.Failure;
        }"""), encoding="utf-8")
    neura(root, "build", source.name, "-o", "values.nvr")
    neura(root, "run", "values.nvr")
    for body, error in (
        ('ui.url("javascript:run()");', "URL scheme not allowed"),
        ('ui.url("http://example.org/a b");', "invalid UI URL character"),
        ('ui.length(1.0, "bad");', "invalid CSS length unit"),
        ("ui.color(16777216);", "CSS color requires a 24-bit RGB value"),
    ):
        source.write_text(program.format(body=body), encoding="utf-8")
        neura(root, "build", source.name, "-o", "values.nvr")
        run = subprocess.run([NEURA, "run", "values.nvr"], cwd=root, env=ENV, capture_output=True, text=True,
                             timeout=60)
        if run.returncode == 0 or error not in run.stderr:
            raise AssertionError(f"expected trap containing {error!r}\n{run.stderr}")


def descriptor_limits(root):
    source = root / "limit.nra"
    for body, limit in (("<p></p>" * 4097, "U-1 nodes"), ("{1}" * 1025, "U-3 dynamic slots")):
        source.write_text("let view = html() { " + body + " };\n", encoding="utf-8")
        diagnostic = neura(root, "check", "--error-format=human", source.name, error="E1015")
        if limit not in diagnostic:
            raise AssertionError(f"expected {limit}\n{diagnostic}")


def main():
    with TemporaryDirectory(prefix="neura-typed-html-") as temporary:
        root = Path(temporary) / "source"
        root.mkdir()
        compare_views(root)
        file_boundaries(root)
        library_values(root)
        descriptor_limits(root)
        (root / "overload.nra").write_text(OVERLOAD, encoding="utf-8")
        neura(root, "build", "overload.nra", "-o", "overload.nvr")
        neura(root, "run", "overload.nvr")


if __name__ == "__main__":
    main()

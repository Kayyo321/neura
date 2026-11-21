"""Build stylesheet forms and check the files, names, links and diagnostics the build produces.

The scoped names are recomputed here from Html030.md: the first 10 characters of the
lowercase base-32 SHA-256 of the package name (empty without a project), the key and the
local name, separated by NUL bytes. Digests are recomputed with hashlib.
"""

import base64
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory


NEURA = Path(sys.argv[1]).resolve()
ENV = dict(os.environ, NEURA_GPU="off")


def scoped(key, name, package=""):
    digest = hashlib.sha256(b"\0".join([package.encode(), key.encode(), name.encode()])).digest()
    return name + "__" + base64.b32encode(digest).decode().lower()[:10]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def run(args, cwd, expect=0):
    result = subprocess.run([str(NEURA), *args], cwd=cwd, env=ENV, capture_output=True, text=True)
    if result.returncode != expect:
        raise AssertionError(f"neura {' '.join(args)} returned {result.returncode}\n{result.stdout}{result.stderr}")
    return result


def check(condition, message):
    if not condition:
        raise AssertionError(message)


MAIN = """\
let console = import:Std.Io.Console;
let program = import:Std.Program;
let theme = import:Theme;

const First = css {
    @import "imports/base.css";
    body { background: url("img/logo.png#mark"); content: "}"; -moz-unknown: 1 /* } */; }
    .icon { mask: url(data:image/gif;base64,R0lGOD==); filter: url(#glow); }
};

const Local = css.module {
    .row { animation: fade 1s; }
    :global(.dark) .row { color: white; }
    @keyframes fade { from { opacity: 0; } }
};

const File = css.module from "./parts.module.css";

const Again = css {
    @import "imports/base.css";
    body { background: url("img/logo.png#mark"); content: "}"; -moz-unknown: 1 /* } */; }
    .icon { mask: url(data:image/gif;base64,R0lGOD==); filter: url(#glow); }
};

let early = func() -> string => Later.class_name("late");

let card = html(title: string) {
    <div class={ Local.class_name("row") }>
        <img src="img/logo.png?v=2" alt="Logo">
        <span>{title}</span>
    </div>
};

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        console.print_line(Local.class_name("row"));
        console.print_line(Local.keyframes_name("fade"));
        console.print_line(File.class_name("part"));
        console.print_line(theme.Sheet.path());
        console.print_line(theme.Local.class_name("row"));
        console.print_line(early());
        console.print_line(First.to_string());
        if (First.equals(Again) && First.hash_code() == Again.hash_code()) {
            console.print_line("equal");
        }
        let view = card("ready");
        return program.ExitCode.Ok;
    };
);

const Later = css.module { .late { color: blue; } };
"""

THEME = """\
export let Theme = module (
    export const Sheet = css { :root { --accent: #a40; } };
    export const Local = css.module { .row { color: red; } };
);
"""

with TemporaryDirectory() as temporary:
    root = Path(temporary)
    (root / "img").mkdir()
    (root / "imports").mkdir()
    logo = b"\x89PNG not really"
    (root / "img/logo.png").write_bytes(logo)
    (root / "imports/base.css").write_bytes(b'@import url(./reset.css);\n.base { background: url(../img/logo.png); }\n')
    (root / "imports/reset.css").write_bytes(b"* { margin: 0; }\r\n")
    # A byte-order mark is ignored, CRLF line ends are kept as written.
    (root / "parts.module.css").write_bytes(b"\xef\xbb\xbf.part { color: blue; }\r\n.part:hover { unknown-thing: {a: b}; }\r\n")
    (root / "theme.nra").write_text(THEME)
    (root / "app.nra").write_text(MAIN)
    out = root / "out"
    out.mkdir()
    run(["build", "app.nra", "-o", "out/app.nvr"], root)
    listing = json.loads((out / "app.nvr.styles.json").read_text())
    files = {entry["path"]: entry for entry in listing["files"]}
    for path, entry in files.items():
        data = (out / path).read_bytes()
        check(sha(data) == entry["sha256"] and len(data) == entry["bytes"], f"{path} does not match its entry")
        check(path.split("/")[-1].split(".")[0] == entry["sha256"], f"{path} is not named by its digest")
    on_disk = sorted(p.relative_to(out).as_posix() for p in out.rglob("*") if p.is_file())
    check(on_disk == sorted([*files, "app.nvr", "app.nvr.styles.json"]), f"unexpected files {on_disk}")

    asset = f"assets/{sha(logo)}.png"
    check(asset in files, "the logo is not bundled")
    reset = b"* { margin: 0; }\r\n"
    reset_path = f"styles/{sha(reset)}.css"
    check(files.get(reset_path) is not None, "the nested import is not bundled byte for byte")
    base = f'@import url({sha(reset)}.css);\n.base {{ background: url(../{asset}); }}\n'.encode()
    base_path = f"styles/{sha(base)}.css"
    check(base_path in files, "the imported stylesheet is not rewritten as expected")

    first_body = (
        '\n    @import "' + sha(base) + '.css";\n'
        '    body { background: url("../' + asset + '#mark"); content: "}"; -moz-unknown: 1 /* } */; }\n'
        "    .icon { mask: url(data:image/gif;base64,R0lGOD==); filter: url(#glow); }\n"
    ).encode()
    first_path = f"styles/{sha(first_body)}.css"
    check(first_path in files, "the inline stylesheet is not rewritten byte for byte")

    row = scoped("app.nra:Local", "row")
    fade = scoped("app.nra:Local", "fade")
    local_body = (
        f"\n    .{row} {{ animation: {fade} 1s; }}\n"
        f"    .dark .{row} {{ color: white; }}\n"
        f"    @keyframes {fade} {{ from {{ opacity: 0; }} }}\n"
    ).encode()
    local_path = f"styles/{sha(local_body)}.css"
    check(local_path in files, "the inline module is not renamed as expected")
    part = scoped("parts.module.css", "part")
    file_body = f".{part} {{ color: blue; }}\r\n.{part}:hover {{ unknown-thing: {{a: b}}; }}\r\n".encode()
    file_path = f"styles/{sha(file_body)}.css"
    check(file_path in files, "the external module is not renamed as expected")

    theme_body = b" :root { --accent: #a40; } "
    theme_path = f"styles/{sha(theme_body)}.css"
    theme_row = scoped("theme.nra:Theme.Local", "row")
    theme_local_path = f"styles/{sha(f' .{theme_row} {{ color: red; }} '.encode())}.css"
    late = scoped("app.nra:Later", "late")
    later_path = f"styles/{sha(f' .{late} {{ color: blue; }} '.encode())}.css"
    # Imported files first, then source order; a sheet with the same digest is linked once.
    expected_links = [theme_path, theme_local_path, first_path, local_path, file_path, later_path]
    check(listing["stylesheets"] == expected_links, f"links {listing['stylesheets']} != {expected_links}")
    check(base_path not in listing["stylesheets"], "an imported sheet is linked from the page")

    output = run(["run", "out/app.nvr"], root).stdout.splitlines()
    check(output == [row, fade, part, theme_path, theme_row, late, f"Stylesheet({sha(first_body)})", "equal"],
          f"output {output}")

    # The descriptor holds the bundled path; the query is kept.
    listing_ir = run(["emit-ir", "app.nra", "--no-optimize"], root).stdout
    check(f'\\"value\\":\\"{asset}?v=2\\"' in listing_ir, "the template image is not rewritten")

    # The same build gives the same bytes.
    second = root / "second"
    second.mkdir()
    run(["build", "app.nra", "-o", "second/app.nvr"], root)
    for path in [*files, "app.nvr", "app.nvr.styles.json"]:
        check((second / path).read_bytes() == (out / path).read_bytes(), f"{path} differs between builds")

    # A program without stylesheets writes no extra file.
    (root / "plain.nra").write_text("let console = import:Std.Io.Console;\nlet program = import:Std.Program;\n"
                                    "let App = module:program.Entry (\n"
                                    "    export let.override main = func(args: string[]) -> program.ExitCode {\n"
                                    "        console.print_line(\"plain\");\n        return program.ExitCode.Ok;\n    };\n);\n")
    plain = root / "plain"
    plain.mkdir()
    run(["build", "plain.nra", "-o", "plain/plain.nvr"], root)
    check(sorted(p.name for p in plain.iterdir()) == ["plain.nvr"], "a plain build wrote stylesheet files")

    # U-8: at most 256 distinct stylesheets.
    many = "".join(f"const S{i} = css {{ .c{i} {{ order: {i}; }} }};\n" for i in range(257))
    (root / "many.nra").write_text(many)
    failure = run(["check", "many.nra", "--color=never"], root, expect=1).stderr
    check("E1015" in failure and "U-8" in failure and "257 exceeds 256" in failure, failure)
    (root / "enough.nra").write_text("".join(f"const S{i} = css {{ .c{i} {{ order: {i}; }} }};\n" for i in range(256)))
    run(["check", "enough.nra"], root)

    # Bundled media beyond src and poster: the strings that begin image-set() options, each srcset URL, and the
    # href of an SVG <use>. A link's href and a MIME type string are not files.
    icons = b"<svg xmlns='http://www.w3.org/2000/svg'><symbol id='tick'/></svg>"
    second_image = b"\x89PNG second"
    avif = b"AVIF bytes"
    (root / "img/b.png").write_bytes(second_image)
    (root / "img/c.avif").write_bytes(avif)
    (root / "img/icons.svg").write_bytes(icons)
    (root / "media.nra").write_text("""\
const Images = css {
    .hero { background-image: image-set("img/logo.png" 1x, url(img/b.png) 2x, "img/c.avif" type("image/avif")); }
    .old { background-image: -webkit-image-set('img/b.png' 1x); }
};

let gallery = html() {
    <picture>
        <source srcset="img/logo.png 1x, img/b.png 2x" type="image/png">
        <img src="img/logo.png" srcset="img/logo.png, img/b.png 2x, https://example.org/x.png 3x" alt="Gallery">
    </picture>
    <svg viewBox="0 0 4 4"><use href="img/icons.svg#tick"/></svg>
    <a href="img/logo.png">Logo</a>
};

let program = import:Std.Program;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode => program.ExitCode.Ok;
);
""")
    media = root / "media"
    media.mkdir()
    run(["build", "media.nra", "-o", "media/media.nvr"], root)
    listing = json.loads((media / "media.nvr.styles.json").read_text())
    paths = {entry["path"] for entry in listing["files"]}
    logo_asset, b_asset = f"assets/{sha(logo)}.png", f"assets/{sha(second_image)}.png"
    avif_asset, icons_asset = f"assets/{sha(avif)}.avif", f"assets/{sha(icons)}.svg"
    check({logo_asset, b_asset, avif_asset, icons_asset} <= paths, f"media files not bundled: {paths}")
    images_body = (
        '\n    .hero { background-image: image-set("../' + logo_asset + '" 1x, url(../' + b_asset + ') 2x, "../' +
        avif_asset + '" type("image/avif")); }\n'
        "    .old { background-image: -webkit-image-set(\"../" + b_asset + "\" 1x); }\n").encode()
    check(f"styles/{sha(images_body)}.css" in paths, "image-set() strings are not rewritten as expected")
    media_ir = run(["emit-ir", "media.nra", "--no-optimize"], root).stdout
    for value in (f"{logo_asset} 1x, {b_asset} 2x", f"{logo_asset}, {b_asset} 2x, https://example.org/x.png 3x",
                  f"{icons_asset}#tick", "img/logo.png"):
        check(f'\\"value\\":\\"{value}\\"' in media_ir, f"descriptor value {value} missing")
    (root / "bad-srcset.nra").write_text('let i = html() { <img srcset="ok.png 1x, javascript:x 2x" alt="x"> };\n')
    failure = run(["check", "bad-srcset.nra", "--color=never"], root, expect=1).stderr
    check("E1007" in failure and "srcset" in failure, failure)
    (root / "missing-srcset.nra").write_text('let i = html() { <img srcset="img/logo.png 1x, img/none.png 2x" alt="x"> };\n')
    failure = run(["check", "missing-srcset.nra", "--color=never"], root, expect=1).stderr
    check("E1013" in failure and "no file exists" in failure, failure)

    # neura docs gives a factory's parameters and result and a stylesheet's form, never their bodies.
    (root / "documented.nra").write_text('/// A row.\nexport let row = html(name: string, count: int32) { <li>{name}</li> };\n'
                                         '/// Styles.\nexport const Styles = css.module { .row { color: red; } };\n'
                                         '/// Shared.\nexport const Shared = css from "./parts.module.css";\n')
    signatures = [entry["signature"] for entry in json.loads(run(["docs", "documented.nra"], root).stdout)["files"][0]["symbols"]]
    check(signatures == ["let row = html(name: string, count: int32) -> ui.View", "const Styles = css.module",
                         'const Shared = css from "./parts.module.css"'], f"docs signatures {signatures}")

print("stylesheet build checks passed")

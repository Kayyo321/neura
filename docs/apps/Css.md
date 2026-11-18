# Stylesheets

A `css` form is a module-level constant that holds a stylesheet. The build writes the stylesheet to a file named by its SHA-256 and the constant holds that digest; the text is not in the image. Nothing in a stylesheet is Neura, and there is no hole in CSS.

<!-- example: check -->
```neura
let ui = import:Std.Ui;

const PartStyles = css {
    .part-list { display: grid; gap: 0.5rem; }
    .part-list .low { color: var(--warning, #a40); }
};

const Local = css.module {
    .row { animation: fade 1s; }
    :global(.dark) .row { color: white; }
    @keyframes fade { from { opacity: 0; } }
};

let row = html(name: string) {
    <div class={ Local.class_name("row") }>{name}</div>
};

let sheet: ui.Stylesheet = PartStyles;
```

## Forms

`css { ... }` has the type `Std.Ui.Stylesheet` and `css.module { ... }` the type `Std.Ui.StyleModule`. Either form is the whole initializer of a module-level `const`; anywhere else it is `E0255`. `css from "./parts.css"` and `css.module from "./parts.module.css"` read the body from a file, with the rules of [external templates](Html.md#external-templates): a relative path inside the directory of the first source file, at most 1 MiB of UTF-8, with an optional byte-order mark that is ignored (`E1011`).

The text is kept as written. Selectors, custom properties, media and container queries, layers, keyframes, nesting, vendor prefixes and properties the compiler does not know pass through byte for byte. The build reports an unterminated string, comment or `url(`, or an unbalanced delimiter, as `E0253`.

## Modules

A `css.module` renames its class selectors and its `@keyframes` names, and the references to those names in `animation-name` and `animation`. `:global(...)` keeps its content as written; `:global` without parentheses and `:local` are `E0256`. A renamed name is the original followed by `__` and ten base-32 characters of the SHA-256 of the package name, the stylesheet's key and the local name. A build without a project has an empty package name. The key of a file is its path below the source root; the key of an inline sheet is the path of its source file, a colon, and the constant's name qualified by its enclosing modules, such as `app.nra:Theme.Local`.

`Local.class_name("row")` and `Local.keyframes_name("fade")` are replaced by the renamed strings at compile time. The receiver is a `css.module` constant declared in source and the argument is a string literal that names a declared class or keyframes rule; anything else is `E1012`.

## Imports and assets

A `url(...)` and an `@import` are resolved relative to the stylesheet's file, or to the declaring source file for an inline sheet. A local `@import` stays an `@import`, so its conditions and layers keep their meaning; the imported file is written under its own digest and the target rewritten. Any other `url(...)` names an asset, which is written as `assets/<digest>.<extension>` and referenced from the stylesheet as `../assets/<digest>.<extension>`. A query or fragment after the path is kept. A fragment-only reference such as `url(#glow)` and a `data:` URL are left as written.

A static `src` of `img`, `source`, `video`, `audio` or `track`, and a static `poster` of `video`, that names a local file is bundled the same way; the template's value becomes `assets/<digest>.<extension>`, relative to the page. `@namespace` URLs name no file and are left as written; so are the bare strings of `image-set()`, which are not bundled in 0.3.0.

A missing file, a path outside the source root, a cycle of imports (listed in the message), and a remote URL are `E1013`. A remote origin requires a project manifest that lists it under `style`; a build without a project lists none.

## Build output and order

`neura build app.nra -o out/app.nvr` writes every stylesheet and asset of the program below `out/` at its bundle path, `styles/<digest>.css` and `assets/<digest>.<extension>`, and writes `out/app.nvr.styles.json`. That canonical JSON lists each file with its path, SHA-256 and byte length, and under `stylesheets` the sheets the page links, in cascade order: every stylesheet constant of the program, its files in module initialization order and each file in source order. An imported sheet is reached through its `@import` and is not linked. A sheet with the same text is linked once. A program without stylesheet forms or bundled assets writes nothing extra.

The order does not depend on when a component mounts, and unmounting removes nothing. A program may have at most 256 distinct stylesheets (`U-8`, `E1015`).

The files come from source: a build from a `.nir` input or with `--native` does not write them, so build an application that uses stylesheets from its sources.

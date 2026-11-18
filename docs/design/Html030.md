# Neura 0.3.0: HTML factories and CSS

This note fixes the grammar and rules of the `html` and `css` forms before they are implemented, as [the 0.3.0 baseline](Release030.md) requires: HTML factories with bindings, conditions and keyed lists, external template files, ordinary stylesheets and CSS modules, the evaluation order of a render, and what a commit does. It is one of the notes of [the application platform](Apps030.md), which owns the limits (`U-1` and the like), the budgets and the code ranges this note refers to. Rows move to `docs/apps/Html.md` and `docs/apps/Css.md` in the change that implements them.

The owner approved the A0 design decisions on 2026-11-17. Rows marked *proposed* await implementation and qualification in A1. Later changes to a row are made here, with the reason.

<!-- example: fragment: the html and css forms are not implemented until A1 -->
```neura
let ui = import:Std.Ui;

const PartStyles = css {
    .part-list { display: grid; gap: 0.5rem; }
    .part-list .low { color: var(--warning, #a40); }
};

let part_row = html(name: string, count: int32, on_restock: func(ui.ClickEvent) -> void) {
    <li class="part">
        <span>{name}</span>
        <output aria-live="polite">{count}</output>
        @if (count < 5) {
            <strong class="low">Low stock</strong>
        }
        <button type="button" on:click={on_restock}>Restock</button>
    </li>
};
```

## Principles

| Rule | Status |
| --- | --- |
| An `html` form is an expression whose value is a function: `html(a: A, b: B) { ... }` has the type `func(A, B) -> ui.View`. It is not a string, not a class, and not script. | proposed |
| A template is real HTML with three additions: a hole `{expression}`, a binding directive (`on:`, `prop:`, `var:`), and a control block (`@if`, `@for`). Nothing else is added, and nothing in a template is evaluated by a browser as code. | proposed |
| A `css` form is a constant: a stylesheet the build writes to a file. It contains no Neura. | proposed |
| The compiler accepts only a template whose tree is the tree a browser would build from it without repair. | proposed |
| Every hole is an ordinary Neura expression, typed, lowered and optimized like any other. A view is built by ordinary code and committed by library code; the host applies a list of operations. | proposed |

## Grammar

```text
html_factory := ["new"] "html" parameter_list (html_block | from_clause)
stylesheet   := "css" ["." "module"] (css_block | from_clause)
html_block   := "{" template "}"
css_block    := "{" css_text "}"
from_clause  := "from" string_literal

template     := node*
node         := element | text | reference | hole | control | comment
element      := "<" name attribute* ">" node* "</" name ">"
              | "<" void_name attribute* ["/"] ">"
              | "<" svg_name attribute* "/>"
attribute    := attr_name ["=" (quoted | hole)]
              | directive "=" hole
directive    := "on:" event ("." modifier)*
              | "prop:" property
              | "var:" custom_property
quoted       := '"' (attr_char | reference)* '"' | "'" (attr_char | reference)* "'"
hole         := "{" expression "}"
reference    := "&" entity_name ";" | "&#" digits ";" | "&#x" hex_digits ";"
comment      := "<!--" comment_text "-->"
control      := if_block | for_block
if_block     := "@if" "(" expression ")" "{" node* "}"
                ("@else" "if" "(" expression ")" "{" node* "}")*
                ["@else" "{" node* "}"]
for_block    := "@for" "(" ("let" | "const") identifier [":" type] "in" expression
                ";" "key" ":" expression ")" "{" node* "}"
```

`parameter_list`, `type`, `expression`, `identifier` and `string_literal` are the language's own. `css_text` is a list of rules by the CSS Syntax tokenizer.

### Where the forms are recognized

`html`, `css`, `from`, `module`, `key`, `if` after `@else`, and the words after `@` are contextual. Each stays an ordinary name everywhere else.

| Rule | Status |
| --- | --- |
| The lexer finds the body. HTML and CSS text cannot pass the Neura lexer (an apostrophe in text, a `#` in a color), and every tool calls `parse(lex(source))`, so the lexer emits the body as one token, `TokenKind::HtmlIsland` or `TokenKind::CssIsland`, whose text is the bytes between the braces and whose span covers the braces. | proposed |
| An HTML body starts at the first `{` after the `)` that matches the parameter list's `(`, where the list follows the identifier `html` and the token before `html` is not `.`. So that the parser, not a flood of lexical errors, reports a malformed factory (`E0240`), the lexer also accepts a balanced `<...>` group between `html` and `(`, and a `->` with the tokens of a type between `)` and `{`, provided no `;` intervenes. | proposed |
| A `{` directly after `html` or `css`, or after `css` `.` and one identifier, starts a body only when the token before `html` or `css` is `=`, `new`, `(`, `return` or `=>`. The restriction exists because a name followed by `{` is valid today in one place: a match arm with named fields, `Variant { a, b } => ...`, which follows `{` or `,`. | proposed |
| None of these token sequences is valid in 0.2. In `src/parser/parser.cpp` a block follows only a keyword, the contextual `scope` and `select`, the `)` of a condition, loop header or parameter list introduced by a keyword, `else`, and `=>`; a call is never followed by a block, and `->` appears only after a `func` parameter list. A binding, function, field, method, enum variant or type named `html` or `css` keeps working: `html(x);`, `page.html(x)`, `let css = 1;`, `css.length`, `func() -> html { ... }` and a match arm `css { a } => ...` contain no body. No `.nra` file under `stdlib`, `examples` or `tests` uses either word. | proposed |
| `from` is recognized only directly after the `)` of an `html` parameter list, or directly after `css` or `css.module`. | proposed |
| The end of an HTML body is the `}` that closes it when the template tokenizer is in its text state at nesting depth zero. The tokenizer has the states *text*, *tag*, *quoted attribute value*, *comment*, *hole* and *control head*. A brace inside a quoted attribute value or a comment is text. Inside a hole or a control head the Neura lexer runs, so a `}` in a Neura string, character or comment does not close the hole. | proposed |
| The end of a CSS body is the `}` that balances it under the CSS tokenizer: strings, comments, `url(...)` and nested blocks are tokens, not counted characters. | proposed |
| `parse` turns an island into `TemplateSyntax` or `StylesheetSyntax` (`src/parser/html_template.cpp`, `css_stylesheet.cpp`). Each hole is lexed and parsed as an expression with spans in the original file. | proposed |

### Templates

| Rule | Status |
| --- | --- |
| Tag and attribute names are written in lowercase, except the SVG names that the table spells with capitals (`viewBox`, `linearGradient`). | proposed |
| A void element (`br`, `col`, `hr`, `img`, `input`, `source`, `track`, `wbr`) has no end tag; a trailing `/` is allowed and ignored. Any other HTML element needs its end tag: `<div/>` is an error, because a browser ignores the slash. An SVG element may be self-closing. | proposed |
| Every optional end tag of HTML is required here: `<li>`, `<p>`, `<td>`, `<option>` and the rest are closed explicitly. | proposed |
| An element may appear only where the browser's tree builder would keep it without moving, closing or inserting anything: no block content in `<p>`, no interactive content in `<a>` or `<button>`, `<tr>` only in `<thead>`, `<tbody>` or `<tfoot>` (never directly in `<table>`), `<li>` only in `<ol>`, `<ul>` or `<menu>`, and so on by the content-model table in `src/parser/html_tables.inc`. A control block is transparent to this check: its body is checked against the enclosing element. | proposed |
| An attribute value is quoted, or is a hole. An unquoted value is an error. A quoted value is static: a brace in it is text, and there is no interpolation inside quotes. A dynamic value is one whole expression, `title={ label + suffix }`. | proposed |
| An attribute name appears once on an element, counting a directive on the same property or event. | proposed |
| In text, `{` starts a hole and `}` alone is an error; write `&#123;` and `&#125;`. `@` starts a control block when it begins a run of text and a letter follows; write `&#64;` for a literal one there. An `@` inside a word (`a@b.example`) is text. | proposed |
| `&` starts a character reference from the HTML table, with its `;`. A bare `&` is an error; write `&amp;`. | proposed |
| A comment is removed at compile time and may contain anything except `--`. A hole inside a comment is text. | proposed |
| Whitespace: outside `<pre>` and `<textarea>`, a run of whitespace that contains a line break and touches a tag, a control block or the body's ends is removed; every other run becomes one space. References such as `&#32;` and `&nbsp;` are not whitespace. Re-indenting a template therefore never changes its tree. | proposed |
| `<script>`, `<style>`, an attribute whose name starts with `on`, the `style` attribute, a doctype, a CDATA section, a processing instruction, and any attribute spread or dynamic tag name are rejected (`E0251`). The `style` attribute is rejected even when static, so that the generated content-security policy needs no inline-style allowance. | proposed |

### Elements, attributes and properties

The tables are frozen data in `src/parser/html_tables.inc`, generated into the language page.

| Group | Members |
| --- | --- |
| Sections | `main` `section` `article` `aside` `header` `footer` `nav` `address` `search` `hgroup` `h1` to `h6` |
| Grouping | `div` `p` `hr` `pre` `blockquote` `ol` `ul` `menu` `li` `dl` `dt` `dd` `figure` `figcaption` |
| Text | `a` `em` `strong` `small` `s` `cite` `q` `dfn` `abbr` `data` `time` `code` `var` `samp` `kbd` `sub` `sup` `i` `b` `u` `mark` `bdi` `bdo` `span` `br` `wbr` `ruby` `rt` `rp` `ins` `del` |
| Media | `img` `picture` `source` `audio` `video` `track` |
| Tables | `table` `caption` `colgroup` `col` `thead` `tbody` `tfoot` `tr` `th` `td` |
| Forms | `form` `label` `input` `button` `select` `optgroup` `option` `textarea` `output` `progress` `meter` `fieldset` `legend` `datalist` |
| Interactive | `details` `summary` `dialog` |
| SVG, static | `svg` `g` `path` `circle` `ellipse` `line` `polyline` `polygon` `rect` `text` `tspan` `title` `desc` `defs` `use` `symbol` `linearGradient` `radialGradient` `stop` `clipPath` `mask` |

| Rule | Status |
| --- | --- |
| Any other tag is `E1001`. Document elements (`html`, `head`, `body`, `title`, `meta`, `link`, `base`), `template`, `slot`, `iframe`, `embed`, `object`, `canvas`, `noscript`, `map` and `area` are not template elements in 0.3.0. A custom element (a name with a hyphen) is `E1001` until a typed integration declaration exists; none is defined in 0.3.0. | proposed |
| Global attributes: `id` `class` `title` `lang` `dir` `hidden` `tabindex` `role` `inert` `autofocus` `enterkeyhint` `inputmode` `spellcheck` `translate`, every `aria-*` and every `data-*`. Each element adds the attributes the table lists for it (`href` on `a`, `type` `name` `value` `placeholder` and the rest on `input`). An attribute that is neither is `E1002`. | proposed |
| Boolean attributes (`disabled` `checked` `selected` `readonly` `required` `multiple` `hidden` `open` `autofocus` `inert` `novalidate` `formnovalidate` `reversed` `controls` `loop` `muted` `default` `playsinline`): written bare, the attribute is present; with a hole the expression is `bool` and the attribute is present when it is true. A quoted value on one is `E1005`. | proposed |
| A dynamic attribute takes a `string`, or an `Optional<string>` whose `None` removes the attribute. Numeric attributes (`colspan` `rowspan` `min` `max` `step` `width` `height` `tabindex` `maxlength` `minlength` `size` `rows` `cols` `start` `span` `value` on `progress`, `meter`, `li` and `data`) also take any integer type, written in decimal. `aria-*` also takes `bool`, written `true` or `false`, and `data-*` any integer type. Anything else, `nil` included, is `E1005`. | proposed |
| URL attributes (`href`, `src`, `poster`, `cite`, `action`, `formaction`): a static value is a relative reference, a fragment, or an absolute URL with the scheme `https`, `http`, `mailto` or `tel`; anything else, `javascript:` and `data:` included, is `E1007`. A dynamic value has the type `ui.Url`, which `ui.url(text)` returns after the same check; a `string` is `E1007`. `srcset` is static only. | proposed |
| A relative `src`, `poster` or `href` of a media element names a file relative to the template's source file. The build hashes the file, copies it into the bundle and rewrites the reference; a missing file is `E1013`. | implemented for `src` and `poster`; a template's bundled path is relative to the page |
| `prop:value` (`input`, `textarea`, `select`; `string`), `prop:checked` and `prop:indeterminate` (`input`; `bool`), `prop:selected` (`option`; `bool`) and `prop:open` (`details`, `dialog`; `bool`) set a DOM property. No other property exists in 0.3.0. | proposed |
| `var:--name={value}` sets a custom property on the element. The value has the type `ui.CssValue`, built by `ui.length`, `ui.color`, `ui.number` and `ui.percent`, each of which carries a number and a unit; no text is spliced into a stylesheet. | proposed |
| `on:event={handler}` takes a callable whose type is `func(E) -> void` for the event's record `E` ([Components](Components030.md#events)). The expression is evaluated when the view is built; `on:click={save()}` calls `save` then and binds what it returns. Modifiers are `prevent_default` and `stop_propagation`, each valid on the events that table lists. | proposed |
| SVG content is static: no hole, directive or control block inside `<svg>`, except `class`, `aria-*` and `var:` on the `svg` element itself. | proposed |

### Stylesheets

| Rule | Status |
| --- | --- |
| A stylesheet form is the whole initializer of a module-level `const`. It has the type `ui.Stylesheet`, or `ui.StyleModule` for `css.module`. `new css`, a `css` form in an expression, in a function or as an argument is `E0255`. | implemented |
| The text is kept as written. Selectors, custom properties, media and container queries, layers, keyframes, nesting, vendor prefixes and properties the compiler has never heard of pass through. The structural reader reports `E0253` for an unterminated string, comment or `url(`, or an unbalanced delimiter in the CSS body. An inline island whose outer closing brace cannot be identified still reports `E0241`. | structural reader implemented |
| There is no hole in CSS. A `{` is a CSS brace. | implemented |
| Nothing is minified in 0.3.0. The build changes three things only: module names, `url(...)` references, and `@import` targets. | implemented |
| `url(...)` and a local `@import` are resolved relative to the stylesheet's source file. The target is hashed, copied into the bundle and the reference rewritten; an `@import` stays an `@import`, so conditions and layers keep their meaning. A cycle of imports, a missing file, or a remote URL whose origin the manifest does not list under `style` is `E1013`. | implemented; manifest `style` origins wait for projects (A6), so every remote reference is `E1013` |
| `css.module` renames class selectors, identifier-spelled `@keyframes` declarations and references to those declarations in `animation-name` and `animation`. The shorthand uses CSS animation keyword precedence. Strings, unrelated identifiers, custom properties and attribute selectors keep their spelling. `:global(...)` is removed and its content kept as written. `:global` without parentheses and `:local` report `E0256`. | implemented |
| A renamed name is `name__h`, where `h` is the first 10 characters of the lowercase base-32 SHA-256 of the package name, the stylesheet's key and the local name, separated by NUL bytes. The key is the source path relative to the package root for a file, and for an inline sheet the path of the declaring source file relative to the package root, a colon, and the constant's name qualified by its enclosing modules (`app.nra:Theme.Local`); see decision 15. The name uses the decoded CSS identifier and is independent of the sheet's content. Rewriting appends the suffix to the original identifier spelling. | module name generation implemented |
| `Styles.class_name("row")` and `Styles.keyframes_name("fade")` are constant strings. The argument is a string literal that names a declared export; anything else is `E1012`. | implemented for constants declared in source; a constant from a `.nir` library is `E1012` |
| Stylesheets are linked from `index.html` in a fixed order: the manifest's `styles` in the order listed, then every stylesheet constant reachable from the target's entry, in module initialization order and then source order. A constant is reachable when its file is: a build from source loads exactly the files its inputs reach by import, and every stylesheet constant of those files is linked, used or not (decision 16). A sheet is listed once, by content digest. The order does not depend on when a component mounts, and unmounting removes nothing. | order implemented and written to `<image>.styles.json`; manifest `styles` and `index.html` wait for bundles (A6) |

### External files

<!-- example: fragment: the html and css forms are not implemented until A1 -->
```neura
let part_row = html(name: string, count: int32) from "./part-row.html";
const PartStyles = css from "./parts.css";
const Local = css.module from "./parts.module.css";
```

| Rule | Status |
| --- | --- |
| The file holds the body alone, without the braces. A template file's holes see the declared parameters and the scope of the declaration, exactly as an inline body would. | implemented |
| The path is a string literal without escapes: relative, with `/` separators, resolved from the directory of the declaring source file. It must stay inside a source or asset root of the declaring package; for a build without a project, inside the directory of the first source file. | implemented without a project |
| The file is UTF-8, at most 1 MiB, with an optional byte-order mark that is ignored. | implemented |
| An inline body and a file with the same text give the same descriptor and the same image, apart from source-map entries. | proposed |
| The build records the digest of every file read. A deployed application reads none of them. | proposed |

### Formatter and editor

| Rule | Status |
| --- | --- |
| `neura fmt` formats the Neura around an island as today. Inside an island it changes only leading indentation, shifting every line by the same amount so the body sits one level inside its declaration, and removes trailing whitespace. It does not reflow markup, and leaves `<pre>`, `<textarea>`, CSS strings and comments byte for byte. A hole that fits on its line is formatted as an expression; a longer one is left as written. | proposed |
| In the editor's recovering lexer, an island with no end runs to the first later line that is indented no deeper than the line of its declaration and begins with `}`, `let`, `last`, `const` or `export`; the rest of the file keeps its scope. The compiler does not recover: it reports `E0241` at the opening brace. | proposed |
| Tokens inside an island are classified for highlighting (tag, attribute, directive, string, hole, control word, CSS selector, property, value). Completion offers tags valid at the cursor's parent, attributes of the element, events and their record types, and exported class names. | proposed |
| Navigation, hover, rename and signature help work inside holes as in any expression. Rename of a factory parameter renames its uses in an external template file. Go-to-definition on a `from` path opens the file, and on `class_name("x")` the selector. | proposed |
| Indexing never runs a factory. | proposed |

## Semantics

### Typing and ownership

| Rule | Status |
| --- | --- |
| A factory follows the callable rules of the language unchanged: grouped parameters, tags, named arguments, defaults and contextual inference apply to it as to a `func`. It has no result annotation and no type parameters. | proposed |
| A factory that captures nothing is a static function value. One that captures and does not escape uses a frame environment. One that captures and escapes is written `new html(...)`, by the rule and with the code that govern `new func` ([Storage](Storage030.md#construction)); it cannot capture an array or a primitive borrow. | proposed |
| Calling a factory allocates the view. That allocation is in the factory's summary; it is not the allocation of the factory's environment. | proposed |
| `ui.View` is a class that stays in its task (`#NotThreadSafe`): a view, a root, a state or a handler table given to another task is `E0602`. | proposed |
| The view retains every handler bound in it. The factory's retention summary says so for each parameter that reaches an `on:` hole, so a caller that passes a capturing function value writes `new func` or `new bound`, by the existing rule. | proposed |
| A text hole takes a `string`; an integer, written in decimal; a `float32` or `float64`, written as `to_string` writes it; or a `ui.View`, which becomes a child. `bool`, `nil`, an enum, a class, `any`, an array, a list and an `Optional` are `E1004`. No `to_string` method of a class is ever called by a hole. | proposed |
| The `@for` source is a `frozen.FrozenList<T>`, or an array that the render does not retain. A list, a `Sequence`, an iterator and a map are `E1008`. | proposed |
| A key is a `string` or a fixed-width integer (`E1009` otherwise). Keys identify siblings of one execution of one `@for`, nowhere else. | proposed |
| Every expression in a template, and the whole body of a function passed as `render`, is render-compatible: it may allocate and read, and may not write a field of an object it did not create, write a global, call a host service, wait, spawn, send, emit an event, or call anything whose summary is unknown. A violation is `E1010` at the expression, with the call chain. The summaries are those of the effect analysis ([Representation](Representation030.md)); a getter or hook reached by a read counts. | proposed |

### Evaluation

A call of a factory builds the whole view before it returns.

| Rule | Status |
| --- | --- |
| Arguments are evaluated and admitted in written order, as for any call. | proposed |
| The body is evaluated in document order: for each element its attributes left to right, then its children. Each hole is evaluated once. | proposed |
| An `@if` condition is evaluated once, then the body of the first true branch, or of `@else`. Nothing in another branch is evaluated. | proposed |
| An `@for` source is evaluated once. Then for each element in index order: the element is bound, the key is evaluated, then the body. | proposed |
| A trap in a hole is a trap of the call. No later hole is evaluated and no view exists. | proposed |
| A hole of type `ui.View` holds the view it was given. A view appears once in one tree, in one root: the second use is found at commit. | proposed |

### Commit

`root.commit(view)` returns `Result<ui.RenderOutcome>`, where `RenderOutcome` is `Committed` or `Invalid(ui.Diagnostic)`. The work is library code in `stdlib/std/ui/internal/reconcile.nra`.

| Rule | Status |
| --- | --- |
| Validation comes first and touches nothing: a view used twice or already mounted (`ViewReused`); two equal keys among the items of one `@for` (`DuplicateKey`, reporting the second item of the first pair in iteration order); a limit `U-4` to `U-6` (`LimitExceeded`). The outcome is `Ok(Invalid(diagnostic))` and the previously committed view stays mounted and live. It is not a trap and no hole is re-evaluated. | proposed |
| `ui.Diagnostic` has the fields `kind: ui.DiagnosticKind`, `message: string`, `slot: uint32`, `index: int64` (the item's position, or -1) and `key: string` (the key as text, or empty). It needs no source map. | proposed |
| Reconciliation compares the new view with the mounted one, position by position. Two views with the same descriptor and fragment are the same node: only slots whose values differ change. Otherwise the subtree is replaced. Within an `@for`, items match by key and keep their nodes, with the fewest moves. | proposed |
| Strings compare by value. A handler compares by identity: a different callable replaces the entry in the root's handler table and leaves the DOM listener in place. | proposed |
| One commit is one host request, whose operations are applied in this order: removals, in the old document order; creations and moves, in the new document order; text, attribute and custom-property changes, in document order; handler changes; property writes. Post-commit callbacks are queued after the request completes. | proposed |
| A property write is skipped when the DOM value already equals it. While an input method is composing in an element, writes of `prop:value` to that element are held and the last one is applied when composition ends. A write to the focused element restores its selection, clamped to the new length. | proposed |
| If the host fails while applying, the result is `Err`, the root is *faulted*, its events stop, and every later commit returns `Err` until `root.reset()`, which empties the root and accepts a fresh view. There is no rollback and the event that led to the commit is not replayed. | proposed |
| A trap ends the execution, as everywhere. The host may then show a static failure screen; no component can catch it. | proposed |

## Lowering and artifacts

| Rule | Status |
| --- | --- |
| A factory lowers to a function whose body calls `Std.Ui.Internal.Build` in evaluation order: one call per hole, a branch per `@if`, a loop per `@for`, and a final call that returns the view. These are ordinary NeuraIR calls, branches and loops. No NeuraIR operation is added and the format version does not change. | proposed |
| The static tree of a template is one **descriptor**: canonical JSON (sorted keys, no insignificant whitespace) held as an ordinary `string` constant. It lists fragments (the body, and the body of each control block), and in each the static elements, attributes and text, and numbered slots with their kinds: text or child, attribute, flag, property, custom property, handler, block. Its first field is the encoding version, 1. | proposed |
| A view is a heap object: its descriptor, its fragment and its slot values. A block slot holds the views of the chosen branch or of the items, with their keys. | proposed |
| The descriptor is executable data. Stripping names and source maps leaves it and the application works. Spans from template source to generated calls are source-map entries, as optional as any other. | proposed |
| The factory's symbol carries the attribute `ui.factory`, which names its descriptor constant. Symbol attributes need no format version. The decompiler prints a factory as `html(...) { ... }` from the descriptor and the calls when the attribute is present, and as a function with `Build` calls when it is not; it does not claim to recover the source's formatting. | proposed |
| A stylesheet constant is a `ui.Stylesheet` holding the SHA-256 of the file the build writes. The text is not in the image. | implemented; the build writes the files beside the image |
| The optimizer treats `Build` calls as the calls they are: allocating, ordered, not removable when the view is used. Folding a hole's constant expression is ordinary folding. | proposed |
| NVR is unchanged. Tiers need no new operation; a render is ordinary code and runs at whatever tier its blocks reach. | proposed |

### Host services

All are version 1, in a new capability group `ui` that the web and desktop hosts and the model host grant, and that `neura run` denies unless `--ui-host=model` is given.

| Service | Arguments | Result |
| --- | --- | --- |
| `neura.ui.root.open` | A selector text | A root handle |
| `neura.ui.root.close` | A root handle | none |
| `neura.ui.template` | A root handle, a descriptor text | A template handle; the host verifies the version and the limits `U-1` to `U-3` |
| `neura.ui.commit` | A root handle, the operation bytes | none, or an error code |
| `neura.ui.event.next` | A root handle, a maximum count | Event records; overlapping and cancellable |
| `neura.ui.stats` | A root handle | Counts of nodes, listeners, handles and queued events |
| `neura.history.push`, `replace`, `current`, `next` | A path text | The current path; `next` overlaps |

Handles are per-execution numbers the host checks against the root and generation that issued them; none is a pointer. The operation encoding and the event record encoding are fixed with their test vectors in `docs/runtime/Providers.md` at step 5.

The **model host**, `src/nvm/ui_provider.cpp`, implements these services over an in-memory tree with the same checks, takes scripted events from a file, and prints the tree as canonical HTML. It is what `neura test` and CTest run, and it makes commits replayable. The adapter `hosts/web/nvm.js` and the model host run one shared corpus of operation batches with expected trees (`tests/apps/ui-ops/`).

## Diagnostics

| Code | Title | Span | Fix |
| --- | --- | --- | --- |
| `E0240` | malformed html factory | The `html` word through the offending token: no parameter list, a result annotation, type parameters, neither body nor `from` | Write `html(parameters) { ... }` |
| `E0241` | unterminated template or stylesheet | The opening brace | Close the body |
| `E0242` | mismatched or unclosed element | The start tag, with a label at the end tag found | Close the element named |
| `E0243` | nesting a browser would repair | The inner start tag, with a label at the parent | Move the element, or use the parent the message names |
| `E0244` | malformed tag | The tag: an uppercase name, `<div/>`, a missing `>`, an end tag for a void element | Write the form the message gives |
| `E0245` | malformed or repeated attribute | The attribute: unquoted value, missing value after `=`, a name used twice | Quote the value, or remove the repeat |
| `E0246` | unknown or malformed character reference | The `&` through the name | `&amp;`, or the reference suggested by spelling |
| `E0247` | malformed hole | The braces: empty, more than one expression, a statement, no closing brace | Write one expression |
| `E0248` | stray brace or control word in text | The character | `&#125;`, `&#123;` or `&#64;` |
| `E0249` | malformed control block | The `@` word through the offending token: an unknown word, a missing condition, a missing `; key:`, `@else` without `@if` | The form from the grammar |
| `E0250` | malformed binding directive | The attribute name: an unknown prefix, an empty name, a quoted value on a directive | `on:event={handler}` |
| `E0251` | not allowed in a template | The element or attribute: `<script>`, `<style>`, `onclick=`, `style=`, a doctype, a spread | The message names the replacement: a handler, a stylesheet, a class, `var:` |
| `E0252` | malformed css declaration | The `css` word through the offending token: `css.` followed by anything but `module`, neither body nor `from` | `css { ... }` or `css.module { ... }` |
| `E0253` | malformed stylesheet | The token that cannot be closed or balanced | Close it |
| `E0254` | malformed from clause | The token after `from`: not a string literal, an escape in the literal | A literal relative path |
| `E0255` | html or css form not allowed here | The form: `new css`, a `css` form that is not a module-level `const` initializer | Move it to a module-level `const` |
| `E0256` | malformed CSS module construct | `:global` without parentheses, `:local` | `:global(.name)` |
| `E1001` | unknown element | The tag name | The nearest known name |
| `E1002` | unknown attribute, property or event | The name | The nearest name valid on this element |
| `E1003` | modifier not valid for this event | The modifier | Remove it, or the events that accept it |
| `E1004` | value cannot be rendered | The hole, with the type | Format it to a `string` before the render, or use `@if` or `@for` |
| `E1005` | attribute or property value of the wrong type | The value, with the type expected | The conversion that applies |
| `E1006` | handler does not match the event | The hole, with both types | A function of the event's record |
| `E1007` | URL not allowed | The value | `ui.url(...)`, or an allowed scheme |
| `E1008` | value cannot be iterated by a template | The source expression | `frozen.copy_of(...)` |
| `E1009` | key of the wrong type | The key expression | A `string` or integer field |
| `E1010` | effect not allowed while rendering | The expression, with the call chain to the effect | Do it in a handler or a command |
| `E1011` | template or stylesheet file cannot be used | The path literal: missing, outside the roots, absolute, not UTF-8, too large | The reason and the roots |
| `E1012` | not a class of the CSS module | The argument: not a literal, or not exported | The nearest export |
| `E1013` | stylesheet import or asset cannot be resolved | The reference, in the `.css` or template source: missing, a cycle (listing it), a remote origin not allowed | The path, or the manifest key |
| `E1014` | invalid static document structure | The second of two equal static `id` values in one template; a `<label for>` that names no static `id` in it; an `<img>` without `alt` | Rename, or add the attribute |
| `E1015` | template or stylesheet exceeds a limit | The body, with the limit's id and both numbers | Split the template, or raise the limit |

The compiler has no warning code assigned for this milestone, so a property or selector that a browser of the matrix does not support is not reported in 0.3.0; it is passed through.

## Edge cases

Accepted:

1. `html() { <p>Ready</p> }`: no parameters, a static view.
2. `html(a, b: string) { ... }`: a grouped parameter list.
3. A tag on a parameter, `html(#Unused x: int) { ... }`.
4. A comment between `)` and `{`, and a line break there.
5. A trailing comma in the parameter list.
6. A parameter whose type is a function type containing parentheses and `->`.
7. A parameter default that is a string containing `{` or `)`.
8. An empty body, `html() { }`: a view with no nodes.
9. A body that is only text, and one that is only a hole.
10. Several top-level nodes: the view is a fragment.
11. `<br>` and `<br/>`; `<input type="text">` with no end tag.
12. `<p>a &lt; b &amp;&amp; c</p>`; `&#x1F600;`; `&nbsp;` kept as a character.
13. `<span title="{not a hole}">`: braces in quotes are text.
14. `<span title='say "hi"'>`: single quotes.
15. `title={ $"{count} parts" }`: an interpolated string inside a hole, with its own braces.
16. A hole containing a string with `}`: `{ label("}") }`.
17. A hole containing a block comment with `}`, and a line comment ending at the line's end before the closing brace on the next line.
18. A hole spanning lines; a hole containing a lambda with a block body.
19. A hole whose expression is a call with named arguments and a generic argument list, `{ pick<string>(from: items) }`.
20. `{child}` where `child: ui.View`; a factory called in a hole, `{ part_row(name: p.name, count: p.count, on_restock: restock) }`.
21. `@if (a) { } @else if (b) { } @else { }`, each on its own line or all on one.
22. `@if` whose body is empty.
23. `@for (let part in parts; key: part.id) { ... }`; `const` in place of `let`; a type annotation on the binding.
24. Nested `@for`, the inner key equal to an outer key: keys are per loop.
25. `@for` over an empty collection: no nodes.
26. `@if` directly inside `<ul>` whose body is `<li>` elements, and `@for` directly inside `<tbody>` whose body is a `<tr>`.
27. An email address in text, `ops@parts.example`.
28. `<!-- {ignored} -->`: removed.
29. `<pre>` content with line breaks and indentation, kept exactly.
30. `disabled`, `disabled={busy}`, `aria-pressed={on}` with `on: bool`, `data-count={count}`.
31. `on:submit.prevent_default={save}`; two modifiers in either order.
32. `on:click={ new func(event: ui.ClickEvent) { state.set(0); } }` in a factory that is itself written `new html`.
33. `prop:value={draft}` together with `on:input={edit}`.
34. `var:--accent={ ui.color(tone) }`.
35. Static inline SVG with `viewBox` and a self-closing `<path/>`.
36. A binding named `html` used as a value: `let html = 3; let y = html + 1;`.
37. A function named `html` called as a statement: `html(x);`.
38. A method call `page.html(x)` and a field `theme.css`.
39. A parameter named `from`, `key` or `module`.
40. `css { }`: an empty stylesheet.
41. CSS with `content: "}"`, with `/* } */`, and with `url(data:image/png;base64,...)` unquoted.
42. CSS nesting, `@media`, `@container`, `@layer`, `@keyframes`, `@supports`, a vendor-prefixed property, and a property the compiler does not know.
43. A custom property whose value contains braces, `--shape: { a: b }`.
44. `css.module { .row { } :global(.dark) .row { } @keyframes fade { } .row { animation: fade 1s; } }`.
45. `Local.class_name("row")` used in `class={ Local.class_name("row") }` and in a string concatenation.
46. `html(...) from "./a.html"` and the same file used by two factories with the same parameters.
47. A template file with a byte-order mark, and one with CRLF line ends: the descriptor equals the LF one.
48. A shadowing `let` inside a hole's lambda that hides a factory parameter.
49. A factory stored in a class field as `new html`, and one passed as an argument where a `func(...) -> ui.View` is expected.
50. Overloads: two factories with different parameter types bound to one name, resolved as functions are.
51. In the editor, an unfinished tag `<butt` at the cursor: completion is offered and the rest of the file keeps its symbols.
52. In the editor, an island with no closing brace: the next declaration is still indexed.

Rejected:

53. `let v = html { <p></p> };`: `E0240`, no parameter list.
54. `html(x: int) -> ui.View { ... }`: `E0240`.
55. `html<T>(x: T) { ... }`: `E0240`.
56. `html(x: int);`: `E0240`, neither body nor `from`.
57. A body with no closing brace at the end of the file: `E0241`.
58. `<div><span></div>`: `E0242` at `<span>`.
59. `<li>One<li>Two`: `E0242`; optional end tags are required.
60. `<p><div></div></p>`: `E0243`.
61. `<table><tr>`: `E0243`, naming `<tbody>`.
62. `<a href="/x"><button>`: `E0243`.
63. `<DIV>`, `<div/>`, `</br>`: `E0244`.
64. `<input type=text>`: `E0245`.
65. `<p class="a" class="b">`, and `value="x"` beside `prop:value={y}`: `E0245`.
66. `Fish & chips`, `&nosuch;`, `&amp` without `;`: `E0246`.
67. `{}`, `{a; b}`, `{ let x = 1 }`: `E0247`.
68. A lone `}` in text: `E0248`.
69. `@while (x) { }`, `@for (let p in parts) { }` without a key, `@else { }` first: `E0249`.
70. `@if count > 0 { }`: `E0249`; the condition is parenthesized.
71. `bind:value={x}`, `on:={f}`, `on:click="save()"`: `E0250`.
72. `<script>`, `<style>`, `onclick="save()"`, `style="color: red"`, `<!doctype html>`, `{...attributes}`, `<{tag}>`: `E0251`.
73. `css.global { }`: `E0252`.
74. An unbalanced delimiter inside a framed CSS body, or an unterminated string in an external stylesheet, reports `E0253`. With only one closing brace in `css { .a { color: red; }` at end of input, the lexer balances the inner CSS block and reports the missing outer delimiter as `E0241`.
75. `css from path_variable`, `html(x: int) from "a" + ".html"`: `E0254`.
76. `new css { }`, `let f = func() { const S = css { }; };`, `use(css { })`: `E0255`.
77. `:global .a { }`: `E0256`.
78. `<blink>`, `<my-widget>`, `<iframe>`: `E1001`.
79. `<div href="/x">`, `prop:scrollTop={y}`, `on:clik={f}`: `E1002`.
80. `on:input.prevent_default={f}`: `E1003`.
81. `{ready}` with `ready: bool`; `{part}` with a class; `{maybe}` with an `Optional<string>`; `{nil}`: `E1004`.
82. `disabled="false"`, `colspan={ratio}` with a `float64`, `title={count}` with an integer: `E1005`.
83. `on:click={ func(event: ui.KeyEvent) { } }`, and `on:click={count}`: `E1006`.
84. `href="javascript:run()"`, `src="data:text/html,..."`, `href={path}` with a `string`: `E1007`.
85. `@for (let p in list_of_parts; ...)` with a `Part[list]`, and with a `Sequence<Part>`: `E1008`.
86. `key: p` with a class, `key: p.ratio` with a float, `key: p.code` with an `Optional<string>`: `E1009`.
87. A hole that calls `console.print_line`, one that calls `state.set`, one that spawns, one that calls a function value of unknown summary: `E1010`.
88. `from "/etc/passwd"`, `from "../../outside.html"`, `from "missing.html"`, a file that is not UTF-8: `E1011`.
89. `Local.class_name(name)` with a variable, `Local.class_name("rwo")`: `E1012`.
90. `@import "a.css"` where `a.css` imports the first; `url(./missing.png)`; `@import url(https://cdn.example/x.css)` with no listed origin: `E1013`.
91. Two elements with `id="total"` in one template; `<img src="./a.png">` without `alt`: `E1014`.
92. A template of 5,000 static nodes under the default limit: `E1015`.
93. A capturing factory returned from a function without `new`: the existing rejection of an escaping function value, with a fix that inserts `new`.
94. A factory capturing a local array, or holding `&count`: the existing escape rejections.
95. A `ui.View` passed to `spawn` or sent on a channel: `E0602`.
96. A hole inside `<svg>`: `E1002`, naming the static rule.

Accepted, added on review of the trigger rule:

101. A match arm with named fields on a variant named `css` or `html`: `css { a, b } => ...` is a pattern, as today.
102. A function whose result type is a class named `html`: `func() -> html { ... }` has an ordinary body.
103. A generic call of a user function, `html<int>(x);`, and a call in a condition, `if (html(a)) { ... }`: no body, because the `)` of the call is not followed by `{`.

Rejected, added on the same review:

104. `use(first, css { })`: the stylesheet is the second argument, so no body is recognized and the first error is a lexical or syntax error inside the braces, not `E0255`.

At run time, not compile time:

97. Two items with the key 7: `Ok(Invalid)` with `DuplicateKey`, `index` of the second, and the previous view still mounted and receiving events.
98. The same view value in two holes: `Ok(Invalid)` with `ViewReused`.
99. A hole that divides by zero: a trap of the factory call; no commit happens.
100. A host failure during a commit: `Err`, then `Err` from every commit until `reset`.

## Order of work

Each step is one commit on the milestone branch with its targeted tests. No step changes the image of a program that does not use the forms.

| Step | Content | Tests |
| --- | --- | --- |
| 1 | This note | Document checks |
| 2 | Island tokens in the lexer, both recovery modes; `E0240`, `E0241`, `E0252`, `E0254`, `E0255` | `tests/ui/parser/` fixtures; lexer unit tests of cases 4 to 7, 13 to 18, 36 to 43 |
| 3 | The template parser and tables; `E0242` to `E0251` | One fixture per rejected case 58 to 72; tree dumps of the accepted cases |
| 4 | Typing of holes, attributes, handlers and keys; descriptors; lowering to `Build`; `Std.Ui.Internal.Build` and `ui.View`; `E1001` to `E1009`, `E1014`, `E1015` | IR dumps; inline and `from` equal; cases 73 to 86 |
| 5 | The model host and the `neura.ui.*` services; reconciliation and commit | The operation corpus; order-of-evaluation traces; cases 97 to 100 |
| 6 | Render compatibility, `E1010`; `new html` and the retention summary of handlers | Cases 87, 93 to 95 |
| 7 | The stylesheet parser, modules, imports and assets; `E0253`, `E0256`, `E1011` to `E1013` | Cascade order fixtures; module name vectors |
| 8 | Formatter and editor support | `tests/tooling/` protocol tests with islands |
| 9 | Decompiler and `neura docs` output for factories | Round trips with and without the attribute |

## Gate and acceptance

- An inline template and its external copy build identical images apart from the source map, from source, from rebuilt NeuraIR and from linked NeuraIR.
- A trace of every hole, admission and key of a fixture with conditions and nested keyed lists equals the hand-derived trace, optimized and unoptimized, at every tier.
- A skipped branch and every hole after a trap leave no trace.
- Duplicate keys are found before any operation is issued, and the mounted view still answers events afterwards.
- Each rejection above has a fixture with its code, span and message.
- The same operation batches give the same tree on the model host, in headless Chromium and in headless Firefox.
- Stylesheet order is the same before and after a component mounts late, unmounts, and after a reload; module names equal the recorded vectors; an unknown property survives byte for byte.
- An image without names and source maps runs the same journeys.
- Every program of the identity and extended sets builds the bytes it built before step 2.

## Migration

Nothing in a 0.2 program changes. A program that assembled markup as text replaces it with factories; text that was concatenated into attributes becomes a whole-expression hole; a handler that was a string becomes a function; styles written as strings become a stylesheet and a class or a `var:` binding.

## Decisions taken without the owner

Taken during A1 on 2026-11-17 by autonomous agents, awaiting owner confirmation.

15. The key of an inline stylesheet is `<source path>:<qualified constant>`, not the qualified name alone. A file's module name cannot be recovered from its path (the mapping from `Foo_Bar` and `FooBar` to file names is not one to one), and two files may declare constants with the same name, which the name alone would give the same scoped names.
16. "Reachable" is reachability of files, not of uses: every stylesheet constant of a loaded file is linked. A constant that nothing reads still styles the page, as a `<link>` would, and the order stays independent of what code runs.
17. Until bundles exist (Projects030 step 4), `neura build -o out/app.nvr` writes the stylesheets and assets beside the image at their bundle paths and lists them, with the link order, in `out/app.nvr.styles.json`, a canonical JSON file with `schema` 1. This is an interim interface for the bundle writer, which takes the same lists from the compiled program; it is not a bundle file.
18. A file imported by `@import` is an ordinary stylesheet, even when the importing sheet is a `css.module`: its names are not scoped and it is not linked from `index.html`.
19. A template's bundled media path is `assets/<digest>.<extension>`, relative to the page. A deep-linked route such as `/parts/7` resolves it against that path; the adapter or `index.html` must resolve it against the bundle's base (A2, A6).

## Approved design decisions

1. The lexer, not the parser, finds an island's end, because every tool lexes a whole file before parsing it. A bare `html {` or `css {` is a body only after `=`, `new`, `(`, `return` or `=>`, which keeps named-field match arms on variants of those names valid.
2. `@else if`, not `@else @if`; a key is required on every `@for`.
3. Every optional end tag is required, uppercase tags are rejected, and a bare `&` is an error: the stricter reading of "no browser repair".
4. Whitespace is normalized by one fixed rule so that formatting cannot change a tree.
5. The `style` attribute is rejected outright, and dynamic style is `var:` with typed values, to keep the content-security policy free of inline styles.
6. `bool` in a text hole is an error rather than the text `true`.
7. A dynamic URL needs the type `ui.Url`; a dynamic asset choice is an `@if` between static elements.
8. SVG is static, custom elements are deferred, and the element set excludes `iframe`, `canvas` and document elements.
9. A stylesheet is a module-level `const` only; nothing is minified; a local `@import` stays an `@import`.
10. Stylesheet order is fixed at build time and written into `index.html`; there is no registration call at run time.
11. A CSS-module name hashes the package, the sheet's key and the local name, not the content; a non-literal `class_name` argument is rejected.
12. Descriptors are canonical JSON in a `string` constant, and reconciliation is Neura library code, so the format versions stay and the adapter decides nothing.
13. Recommitting the mounted view object is `ViewReused`.
14. Browser-compatibility notes for CSS are not reported, for want of a warning code.

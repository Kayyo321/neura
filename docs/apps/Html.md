# HTML factories

An `html` expression defines a function that constructs a task-local `Std.Ui.View`. Its parameters are visible in the template, and the return type is inferred from the form. A named declaration can supply alternatives joined by `or html`.

<!-- example: check -->
```neura
let ui = import:Std.Ui;

let entry = html(label: string, count: int32) {
    <div data-count={count}>
        <span>{label}</span>
        <output>{count}</output>
    </div>
};

let ready: ui.View = entry("Ready", 7);
```

## Contents and evaluation

Write real element start and end tags, quoted attributes, and character references with their semicolon. A brace in a quoted value is literal. An unquoted hole supplies one whole Neura expression. Use a character reference such as `&#125;` for a literal brace in text.

A text hole accepts a string, an integer in decimal, a `float32` or `float64` formatted with `to_string`, or a `View` inserted as a child. A dynamic attribute accepts a string or an `Optional<string>`, where `None` removes the attribute. Boolean attributes take a bare name or a Boolean hole. Numeric attributes and `data-*` also accept integers, and `aria-*` accepts Booleans.

`on:event={handler}` binds a function of the event's record type. `prop:name={value}` sets a supported DOM property, and `var:--name={value}` takes a `ui.CssValue`. Dynamic URL attributes take a `ui.Url`, constructed with `ui.url(text)`. Static URLs accept relative references and the schemes `https`, `http`, `mailto`, and `tel`.

A static `src` of `img`, `source`, `video`, `audio` or `track`, or `poster` of `video`, that names a local file is copied into the build output under its digest, and the template refers to the copy ([Stylesheets](Css.md#imports-and-assets)). A class name from a CSS module is `Styles.class_name("row")` ([Stylesheets](Css.md#modules)).

Holes evaluate once in document order, with an element's attributes evaluated before its children. `@if` evaluates conditions in order and renders the first matching branch. `@for` evaluates its source once, then evaluates the key before the body on each iteration. An array supplies items in index order. A key is a string or a fixed-width integer.

<!-- example: check -->
```neura
let shelf = html(counts: int32[], show: bool) {
    @if (show) {
        <ul>
            @for (const count in counts; key: count) {
                <li>{count}</li>
            }
        </ul>
    } @else {
        <p>Hidden</p>
    }
};
```

## External templates

A form written `html(parameters) from "./view.html"` reads the body from that file. Paths are relative to the declaring source file. In a standalone build the resolved path stays within the directory of the first source file. A file holds at most 1 MiB of UTF-8, optionally beginning with a byte-order mark. Diagnostics for expressions inside it identify the original template file.

The build reads the source, records its digest, and compiles the same descriptor an inline body produces. The descriptor and compiled functions are sufficient to construct the view from the image.

## Artifact and limits

The compiler emits the descriptor as an ordinary string constant. Calls to the library builder allocate the view and store the evaluated values. Branches and loops are ordinary NeuraIR. The `ui.factory` attribute points to the descriptor constant in NeuraIR. Source maps identify the original source spans.

The default build limits are 4,096 template nodes, 64 levels of element and control nesting, and 1,024 dynamic slots. Template and binding errors have codes in the [application diagnostics index](../diagnostics/README.md#e10xx-applications).

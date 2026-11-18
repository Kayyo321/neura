<!-- Generated from docs/apps/Html.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# HTML factories

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

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

A static `src` of `img`, `source`, `video`, `audio` or `track`, a `poster` of `video`, each URL of a `srcset` of `img` or `source`, or an `href` of an SVG `<use>`, that names a local file is copied into the build output under its digest, and the template refers to the copy ([Stylesheets](Stylesheets#imports-and-assets)). Each `srcset` URL follows the rule of static URLs, without `mailto` and `tel`. A class name from a CSS module is `Styles.class_name("row")` ([Stylesheets](Stylesheets#modules)).

## Rendering has no effects

Every expression in a template runs while the view is built, so it must not change anything that existed before. A hole may allocate, read bindings, fields and collections, and call functions that do the same. It may not assign a module binding, write a field or element of an object it did not create, make a host request such as printing, start or wait for tasks, or call a function value, whose effects the compiler cannot see; any of these is `E1010`, reported at the hole with the chain of calls that reaches the effect. A handler bound with `on:` runs later, when its event arrives, so its body may have effects.

<!-- example: check -->
```neura
let ui = import:Std.Ui;

let total = func(counts: int32[]) -> int32 {
    let sum: int32 = 0;
    for (let count in counts) {
        sum += count;
    }
    return sum;
};

let summary = html(counts: int32[], reset: func(ui.ClickEvent) -> void) {
    <p>{ total(counts) } parts</p>
    <button type="button" on:click={reset}>Reset</button>
};
```

A view keeps every handler bound in it, so a factory's retention summary says that a parameter reaching an `on:` hole is retained by the view it returns. A function value that captures a local and is passed for such a parameter therefore escapes with the view, and is written `new func`, as for any escaping function value.

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

The default build limits are 4,096 template nodes, 64 levels of element and control nesting, and 1,024 dynamic slots. Template and binding errors have codes in the [application diagnostics index](Diagnostics#e10xx-applications).

# `Std.Ui`

[Standard library index](Stdlib) · [HTML factories](HtmlFactories) · [Language status](Status)

Import this module as `Std.Ui`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra) defines view, URL, custom property, and event types.

## Detailed contracts

An HTML factory returns a task-local `View` carrying its descriptor and evaluated values. See [HTML factories](HtmlFactories) for template syntax, binding types, and evaluation order.

`url(text)` checks a relative reference or an allowed `http`, `https`, `mailto`, or `tel` scheme. ASCII control characters, spaces, and backslashes cause a trap. `length` takes a finite number and one of `px`, `em`, `rem`, `ch`, `vw`, `vh`, `cm`, `mm`, `in`, `pt`, or `pc`. `color` takes a 24-bit RGB value. `number` and `percent` take finite numbers.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Ui** · [`export let View = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L9)
  One rendered fragment and its evaluated slot values. Tagged `#Final`, `#NotThreadSafe`.
- **Ui.View** · [`export last descriptor: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L11)
- **Ui.View** · [`export last fragment: uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L13)
- **Ui.View** · [`export last slots: storage.Slot`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L15)
- **Ui.View** · [`export let owner: class = nil`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L18)
  The reconciler that last placed this view in a tree, and in which pass.
- **Ui.View** · [`export let pass: uint64 = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L20)
- **Ui.View** · [`export let init = func(descriptor: string, fragment: uint32, slots: storage.Slot)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L23)
- **Ui.View** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L25)
- **Ui** · [`export let Url = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L30)
  A URL checked for use in a template attribute. Tagged `#Final`, `#Immutable`.
- **Ui.Url** · [`export last text: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L32)
- **Ui.Url** · [`export let init = func(text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L35)
- **Ui** · [`export let CssValue = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L40)
  A numeric CSS custom property value and its unit. Tagged `#Final`, `#Immutable`.
- **Ui.CssValue** · [`export last value: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L42)
- **Ui.CssValue** · [`export last unit: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L44)
- **Ui.CssValue** · [`export let init = func(value: float64, unit: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L47)
- **Ui** · [`export let Stylesheet = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L55)
  A stylesheet that the build wrote beside the image, named by the SHA-256 of its text. The text is not in the image. The bundle links the file from `index.html`; holding the value keeps nothing alive and registers nothing at run time. Tagged `#Final`, `#Immutable`. Since 0.3.0.
- **Ui.Stylesheet** · [`export last digest: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L57)
  The lowercase hexadecimal SHA-256 of the written file.
- **Ui.Stylesheet** · [`export let init = func(digest: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L60)
- **Ui.Stylesheet** · [`export let path = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L63)
  The bundle path of the file, `styles/<digest>.css`.
- **Ui.Stylesheet** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L65)
- **Ui.Stylesheet** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L67)
- **Ui.Stylesheet** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L74)
- **Ui** · [`export let StyleModule = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L82)
  A CSS module the build wrote beside the image, with its class and keyframes names scoped. Use the constant's `class_name("name")` and `keyframes_name("name")`, which the compiler replaces with the scoped names; their argument is a string literal naming a declared export. Tagged `#Final`, `#Immutable`. Since 0.3.0.
- **Ui.StyleModule** · [`export last digest: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L84)
  The lowercase hexadecimal SHA-256 of the written file.
- **Ui.StyleModule** · [`export let init = func(digest: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L87)
- **Ui.StyleModule** · [`export let path = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L90)
  The bundle path of the file, `styles/<digest>.css`.
- **Ui.StyleModule** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L92)
- **Ui.StyleModule** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L94)
- **Ui.StyleModule** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L101)
- **Ui** · [`export let DiagnosticKind = enum (  ViewReused,  DuplicateKey,  LimitExceeded )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L106)
  Why a commit was refused before anything changed. Since 0.3.0.
- **Ui** · [`export let Diagnostic = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L118)
  A refused commit: what, which slot, and for a keyed item its position and key. Tagged `#Final`, `#Immutable`. Since 0.3.0.
- **Ui.Diagnostic** · [`export last kind: DiagnosticKind`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L119)
- **Ui.Diagnostic** · [`export last message: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L120)
- **Ui.Diagnostic** · [`export last slot: uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L122)
  The slot of the template whose value was refused.
- **Ui.Diagnostic** · [`export last index: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L124)
  The item's position in its `@for`, or -1.
- **Ui.Diagnostic** · [`export last key: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L126)
  The item's key as text, or empty.
- **Ui.Diagnostic** · [`export let init = func(kind: DiagnosticKind, message: string, slot: uint32, index: int64, key: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L129)
- **Ui.Diagnostic** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L131)
- **Ui** · [`export let RenderOutcome = enum (  Committed,  Invalid(Diagnostic) )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L136)
  The outcome of a commit that did not fail in the host. Since 0.3.0.
- **Ui** · [`export let url = func(text: string) -> Url`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L144)
  Returns a URL after checking that it is a reference or uses an allowed scheme.
- **Ui** · [`export let length = func(value: float64, unit: string) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L175)
  A CSS length with a supported unit.
- **Ui** · [`export let color = func(rgb: uint32) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L184)
  A CSS color from a 24-bit RGB value.
- **Ui** · [`export let number = func(value: float64) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L192)
  A unitless CSS number.
- **Ui** · [`export let percent = func(value: float64) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L195)
  A CSS percentage.
- **Ui** · [`export let PointerKind = enum (Mouse, Pen, Touch, Other)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L198)
  The device that supplied a pointer event.
- **Ui** · [`export let Modifiers = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L201)
  Tagged `#Final`, `#Immutable`.
- **Ui.Modifiers** · [`export last alt: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L202)
- **Ui.Modifiers** · [`export last ctrl: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L203)
- **Ui.Modifiers** · [`export last meta: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L204)
- **Ui.Modifiers** · [`export last shift: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L205)
- **Ui.Modifiers** · [`export let init = func(alt: bool, ctrl: bool, meta: bool, shift: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L208)
- **Ui** · [`export let ClickEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L212)
  Tagged `#Final`, `#Immutable`.
- **Ui.ClickEvent** · [`export last button: uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L213)
- **Ui.ClickEvent** · [`export last x: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L214)
- **Ui.ClickEvent** · [`export last y: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L215)
- **Ui.ClickEvent** · [`export last modifiers: Modifiers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L216)
- **Ui.ClickEvent** · [`export last detail: int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L217)
- **Ui.ClickEvent** · [`export let init = func(button: uint8, x: float64, y: float64, modifiers: Modifiers, detail: int32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L220)
- **Ui** · [`export let InputEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L224)
  Tagged `#Final`, `#Immutable`.
- **Ui.InputEvent** · [`export last value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L225)
- **Ui.InputEvent** · [`export last checked: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L226)
- **Ui.InputEvent** · [`export last composing: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L227)
- **Ui.InputEvent** · [`export let init = func(value: string, checked: bool, composing: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L230)
- **Ui** · [`export let ChangeEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L234)
  Tagged `#Final`, `#Immutable`.
- **Ui.ChangeEvent** · [`export last value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L235)
- **Ui.ChangeEvent** · [`export last checked: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L236)
- **Ui.ChangeEvent** · [`export let init = func(value: string, checked: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L239)
- **Ui** · [`export let SubmitEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L243)
  Tagged `#Final`, `#Immutable`.
- **Ui.SubmitEvent** · [`export last submitter: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L244)
- **Ui.SubmitEvent** · [`export let init = func(submitter: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L247)
- **Ui** · [`export let KeyEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L251)
  Tagged `#Final`, `#Immutable`.
- **Ui.KeyEvent** · [`export last key: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L252)
- **Ui.KeyEvent** · [`export last code: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L253)
- **Ui.KeyEvent** · [`export last modifiers: Modifiers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L254)
- **Ui.KeyEvent** · [`export last repeat: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L255)
- **Ui.KeyEvent** · [`export let init = func(key: string, code: string, modifiers: Modifiers, repeat: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L258)
- **Ui** · [`export let FocusEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L262)
  Tagged `#Final`, `#Immutable`.
- **Ui** · [`export let PointerEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L266)
  Tagged `#Final`, `#Immutable`.
- **Ui.PointerEvent** · [`export last x: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L267)
- **Ui.PointerEvent** · [`export last y: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L268)
- **Ui.PointerEvent** · [`export last button: uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L269)
- **Ui.PointerEvent** · [`export last kind: PointerKind`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L270)
- **Ui.PointerEvent** · [`export last modifiers: Modifiers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L271)
- **Ui.PointerEvent** · [`export let init = func(x: float64, y: float64, button: uint8, kind: PointerKind, modifiers: Modifiers)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L274)
- **Ui** · [`export let ScrollEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L278)
  Tagged `#Final`, `#Immutable`.
- **Ui.ScrollEvent** · [`export last top: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L279)
- **Ui.ScrollEvent** · [`export last left: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L280)
- **Ui.ScrollEvent** · [`export let init = func(top: float64, left: float64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L283)
- **Ui** · [`export let ToggleEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L287)
  Tagged `#Final`, `#Immutable`.
- **Ui.ToggleEvent** · [`export last open: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L288)
- **Ui.ToggleEvent** · [`export let init = func(open: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L291)
- **Ui** · [`export let CloseEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L295)
  Tagged `#Final`, `#Immutable`.
- **Ui.CloseEvent** · [`export last return_value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L296)
- **Ui.CloseEvent** · [`export let init = func(return_value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L299)
<!-- END SOURCE API -->

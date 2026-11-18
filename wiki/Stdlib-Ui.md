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
- **Ui.View** · [`export let init = func(descriptor: string, fragment: uint32, slots: storage.Slot)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L18)
- **Ui** · [`export let Url = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L23)
  A URL checked for use in a template attribute. Tagged `#Final`, `#Immutable`.
- **Ui.Url** · [`export last text: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L25)
- **Ui.Url** · [`export let init = func(text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L28)
- **Ui** · [`export let CssValue = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L33)
  A numeric CSS custom property value and its unit. Tagged `#Final`, `#Immutable`.
- **Ui.CssValue** · [`export last value: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L35)
- **Ui.CssValue** · [`export last unit: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L37)
- **Ui.CssValue** · [`export let init = func(value: float64, unit: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L40)
- **Ui** · [`export let Stylesheet = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L48)
  A stylesheet that the build wrote beside the image, named by the SHA-256 of its text. The text is not in the image. The bundle links the file from `index.html`; holding the value keeps nothing alive and registers nothing at run time. Tagged `#Final`, `#Immutable`. Since 0.3.0.
- **Ui.Stylesheet** · [`export last digest: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L50)
  The lowercase hexadecimal SHA-256 of the written file.
- **Ui.Stylesheet** · [`export let init = func(digest: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L53)
- **Ui.Stylesheet** · [`export let path = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L56)
  The bundle path of the file, `styles/<digest>.css`.
- **Ui.Stylesheet** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L58)
- **Ui.Stylesheet** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L60)
- **Ui.Stylesheet** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L67)
- **Ui** · [`export let StyleModule = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L75)
  A CSS module the build wrote beside the image, with its class and keyframes names scoped. Use the constant's `class_name("name")` and `keyframes_name("name")`, which the compiler replaces with the scoped names; their argument is a string literal naming a declared export. Tagged `#Final`, `#Immutable`. Since 0.3.0.
- **Ui.StyleModule** · [`export last digest: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L77)
  The lowercase hexadecimal SHA-256 of the written file.
- **Ui.StyleModule** · [`export let init = func(digest: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L80)
- **Ui.StyleModule** · [`export let path = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L83)
  The bundle path of the file, `styles/<digest>.css`.
- **Ui.StyleModule** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L85)
- **Ui.StyleModule** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L87)
- **Ui.StyleModule** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L94)
- **Ui** · [`export let url = func(text: string) -> Url`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L98)
  Returns a URL after checking that it is a reference or uses an allowed scheme.
- **Ui** · [`export let length = func(value: float64, unit: string) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L129)
  A CSS length with a supported unit.
- **Ui** · [`export let color = func(rgb: uint32) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L138)
  A CSS color from a 24-bit RGB value.
- **Ui** · [`export let number = func(value: float64) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L146)
  A unitless CSS number.
- **Ui** · [`export let percent = func(value: float64) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L149)
  A CSS percentage.
- **Ui** · [`export let PointerKind = enum (Mouse, Pen, Touch, Other)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L152)
  The device that supplied a pointer event.
- **Ui** · [`export let Modifiers = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L155)
  Tagged `#Final`, `#Immutable`.
- **Ui.Modifiers** · [`export last alt: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L156)
- **Ui.Modifiers** · [`export last ctrl: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L157)
- **Ui.Modifiers** · [`export last meta: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L158)
- **Ui.Modifiers** · [`export last shift: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L159)
- **Ui.Modifiers** · [`export let init = func(alt: bool, ctrl: bool, meta: bool, shift: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L162)
- **Ui** · [`export let ClickEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L166)
  Tagged `#Final`, `#Immutable`.
- **Ui.ClickEvent** · [`export last button: uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L167)
- **Ui.ClickEvent** · [`export last x: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L168)
- **Ui.ClickEvent** · [`export last y: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L169)
- **Ui.ClickEvent** · [`export last modifiers: Modifiers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L170)
- **Ui.ClickEvent** · [`export last detail: int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L171)
- **Ui.ClickEvent** · [`export let init = func(button: uint8, x: float64, y: float64, modifiers: Modifiers, detail: int32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L174)
- **Ui** · [`export let InputEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L178)
  Tagged `#Final`, `#Immutable`.
- **Ui.InputEvent** · [`export last value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L179)
- **Ui.InputEvent** · [`export last checked: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L180)
- **Ui.InputEvent** · [`export last composing: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L181)
- **Ui.InputEvent** · [`export let init = func(value: string, checked: bool, composing: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L184)
- **Ui** · [`export let ChangeEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L188)
  Tagged `#Final`, `#Immutable`.
- **Ui.ChangeEvent** · [`export last value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L189)
- **Ui.ChangeEvent** · [`export last checked: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L190)
- **Ui.ChangeEvent** · [`export let init = func(value: string, checked: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L193)
- **Ui** · [`export let SubmitEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L197)
  Tagged `#Final`, `#Immutable`.
- **Ui.SubmitEvent** · [`export last submitter: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L198)
- **Ui.SubmitEvent** · [`export let init = func(submitter: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L201)
- **Ui** · [`export let KeyEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L205)
  Tagged `#Final`, `#Immutable`.
- **Ui.KeyEvent** · [`export last key: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L206)
- **Ui.KeyEvent** · [`export last code: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L207)
- **Ui.KeyEvent** · [`export last modifiers: Modifiers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L208)
- **Ui.KeyEvent** · [`export last repeat: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L209)
- **Ui.KeyEvent** · [`export let init = func(key: string, code: string, modifiers: Modifiers, repeat: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L212)
- **Ui** · [`export let FocusEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L216)
  Tagged `#Final`, `#Immutable`.
- **Ui** · [`export let PointerEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L220)
  Tagged `#Final`, `#Immutable`.
- **Ui.PointerEvent** · [`export last x: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L221)
- **Ui.PointerEvent** · [`export last y: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L222)
- **Ui.PointerEvent** · [`export last button: uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L223)
- **Ui.PointerEvent** · [`export last kind: PointerKind`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L224)
- **Ui.PointerEvent** · [`export last modifiers: Modifiers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L225)
- **Ui.PointerEvent** · [`export let init = func(x: float64, y: float64, button: uint8, kind: PointerKind, modifiers: Modifiers)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L228)
- **Ui** · [`export let ScrollEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L232)
  Tagged `#Final`, `#Immutable`.
- **Ui.ScrollEvent** · [`export last top: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L233)
- **Ui.ScrollEvent** · [`export last left: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L234)
- **Ui.ScrollEvent** · [`export let init = func(top: float64, left: float64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L237)
- **Ui** · [`export let ToggleEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L241)
  Tagged `#Final`, `#Immutable`.
- **Ui.ToggleEvent** · [`export last open: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L242)
- **Ui.ToggleEvent** · [`export let init = func(open: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L245)
- **Ui** · [`export let CloseEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L249)
  Tagged `#Final`, `#Immutable`.
- **Ui.CloseEvent** · [`export last return_value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L250)
- **Ui.CloseEvent** · [`export let init = func(return_value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L253)
<!-- END SOURCE API -->

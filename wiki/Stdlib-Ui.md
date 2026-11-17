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
- **Ui** · [`export let url = func(text: string) -> Url`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L44)
  Returns a URL after checking that it is a reference or uses an allowed scheme.
- **Ui** · [`export let length = func(value: float64, unit: string) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L75)
  A CSS length with a supported unit.
- **Ui** · [`export let color = func(rgb: uint32) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L84)
  A CSS color from a 24-bit RGB value.
- **Ui** · [`export let number = func(value: float64) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L92)
  A unitless CSS number.
- **Ui** · [`export let percent = func(value: float64) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L95)
  A CSS percentage.
- **Ui** · [`export let PointerKind = enum (Mouse, Pen, Touch, Other)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L98)
  The device that supplied a pointer event.
- **Ui** · [`export let Modifiers = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L101)
  Tagged `#Final`, `#Immutable`.
- **Ui.Modifiers** · [`export last alt: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L102)
- **Ui.Modifiers** · [`export last ctrl: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L103)
- **Ui.Modifiers** · [`export last meta: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L104)
- **Ui.Modifiers** · [`export last shift: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L105)
- **Ui.Modifiers** · [`export let init = func(alt: bool, ctrl: bool, meta: bool, shift: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L108)
- **Ui** · [`export let ClickEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L112)
  Tagged `#Final`, `#Immutable`.
- **Ui.ClickEvent** · [`export last button: uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L113)
- **Ui.ClickEvent** · [`export last x: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L114)
- **Ui.ClickEvent** · [`export last y: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L115)
- **Ui.ClickEvent** · [`export last modifiers: Modifiers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L116)
- **Ui.ClickEvent** · [`export last detail: int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L117)
- **Ui.ClickEvent** · [`export let init = func(button: uint8, x: float64, y: float64, modifiers: Modifiers, detail: int32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L120)
- **Ui** · [`export let InputEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L124)
  Tagged `#Final`, `#Immutable`.
- **Ui.InputEvent** · [`export last value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L125)
- **Ui.InputEvent** · [`export last checked: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L126)
- **Ui.InputEvent** · [`export last composing: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L127)
- **Ui.InputEvent** · [`export let init = func(value: string, checked: bool, composing: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L130)
- **Ui** · [`export let ChangeEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L134)
  Tagged `#Final`, `#Immutable`.
- **Ui.ChangeEvent** · [`export last value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L135)
- **Ui.ChangeEvent** · [`export last checked: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L136)
- **Ui.ChangeEvent** · [`export let init = func(value: string, checked: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L139)
- **Ui** · [`export let SubmitEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L143)
  Tagged `#Final`, `#Immutable`.
- **Ui.SubmitEvent** · [`export last submitter: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L144)
- **Ui.SubmitEvent** · [`export let init = func(submitter: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L147)
- **Ui** · [`export let KeyEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L151)
  Tagged `#Final`, `#Immutable`.
- **Ui.KeyEvent** · [`export last key: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L152)
- **Ui.KeyEvent** · [`export last code: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L153)
- **Ui.KeyEvent** · [`export last modifiers: Modifiers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L154)
- **Ui.KeyEvent** · [`export last repeat: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L155)
- **Ui.KeyEvent** · [`export let init = func(key: string, code: string, modifiers: Modifiers, repeat: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L158)
- **Ui** · [`export let FocusEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L162)
  Tagged `#Final`, `#Immutable`.
- **Ui** · [`export let PointerEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L166)
  Tagged `#Final`, `#Immutable`.
- **Ui.PointerEvent** · [`export last x: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L167)
- **Ui.PointerEvent** · [`export last y: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L168)
- **Ui.PointerEvent** · [`export last button: uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L169)
- **Ui.PointerEvent** · [`export last kind: PointerKind`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L170)
- **Ui.PointerEvent** · [`export last modifiers: Modifiers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L171)
- **Ui.PointerEvent** · [`export let init = func(x: float64, y: float64, button: uint8, kind: PointerKind, modifiers: Modifiers)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L174)
- **Ui** · [`export let ScrollEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L178)
  Tagged `#Final`, `#Immutable`.
- **Ui.ScrollEvent** · [`export last top: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L179)
- **Ui.ScrollEvent** · [`export last left: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L180)
- **Ui.ScrollEvent** · [`export let init = func(top: float64, left: float64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L183)
- **Ui** · [`export let ToggleEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L187)
  Tagged `#Final`, `#Immutable`.
- **Ui.ToggleEvent** · [`export last open: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L188)
- **Ui.ToggleEvent** · [`export let init = func(open: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L191)
- **Ui** · [`export let CloseEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L195)
  Tagged `#Final`, `#Immutable`.
- **Ui.CloseEvent** · [`export last return_value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L196)
- **Ui.CloseEvent** · [`export let init = func(return_value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L199)
<!-- END SOURCE API -->

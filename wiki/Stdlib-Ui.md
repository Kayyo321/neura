# `Std.Ui`

[Standard library index](Stdlib) · [HTML factories](HtmlFactories) · [Language status](Status)

Import this module as `Std.Ui`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra) defines view, URL, custom property, and event types.

## Detailed contracts

An HTML factory returns a task-local `View` carrying its descriptor and evaluated values. See [HTML factories](HtmlFactories) for template syntax, binding types, and evaluation order.

`url(text)` checks a relative reference or an allowed `http`, `https`, `mailto`, or `tel` scheme. ASCII control characters, spaces, and backslashes cause a trap. `length` takes a finite number and one of `px`, `em`, `rem`, `ch`, `vw`, `vh`, `cm`, `mm`, `in`, `pt`, or `pc`. `color` takes a 24-bit RGB value. `number` and `percent` take finite numbers.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Ui** · [`export let View = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L14)
  One rendered fragment and its evaluated slot values. Tagged `#Final`, `#NotThreadSafe`.
- **Ui.View** · [`export last descriptor: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L16)
- **Ui.View** · [`export last fragment: uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L18)
- **Ui.View** · [`export last slots: storage.Slot`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L20)
- **Ui.View** · [`export let init = func(descriptor: string, fragment: uint32, slots: storage.Slot)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L23)
- **Ui** · [`export let Url = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L28)
  A URL checked for use in a template attribute. Tagged `#Final`, `#Immutable`.
- **Ui.Url** · [`export last text: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L30)
- **Ui.Url** · [`export let init = func(text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L33)
- **Ui** · [`export let CssValue = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L38)
  A numeric CSS custom property value and its unit. Tagged `#Final`, `#Immutable`.
- **Ui.CssValue** · [`export last value: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L40)
- **Ui.CssValue** · [`export last unit: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L42)
- **Ui.CssValue** · [`export let init = func(value: float64, unit: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L45)
- **Ui** · [`export let url = func(text: string) -> Url`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L49)
  Returns a URL after checking that it is a reference or uses an allowed scheme.
- **Ui** · [`export let length = func(value: float64, unit: string) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L80)
  A CSS length with a supported unit.
- **Ui** · [`export let color = func(rgb: uint32) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L89)
  A CSS color from a 24-bit RGB value.
- **Ui** · [`export let number = func(value: float64) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L97)
  A unitless CSS number.
- **Ui** · [`export let percent = func(value: float64) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L100)
  A CSS percentage.
- **Ui** · [`export let PointerKind = enum (Mouse, Pen, Touch, Other)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L103)
  The device that supplied a pointer event.
- **Ui** · [`export let Modifiers = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L106)
  Tagged `#Final`, `#Immutable`.
- **Ui.Modifiers** · [`export last alt: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L107)
- **Ui.Modifiers** · [`export last ctrl: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L108)
- **Ui.Modifiers** · [`export last meta: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L109)
- **Ui.Modifiers** · [`export last shift: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L110)
- **Ui.Modifiers** · [`export let init = func(alt: bool, ctrl: bool, meta: bool, shift: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L113)
- **Ui** · [`export let ClickEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L117)
  Tagged `#Final`, `#Immutable`.
- **Ui.ClickEvent** · [`export last button: uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L118)
- **Ui.ClickEvent** · [`export last x: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L119)
- **Ui.ClickEvent** · [`export last y: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L120)
- **Ui.ClickEvent** · [`export last modifiers: Modifiers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L121)
- **Ui.ClickEvent** · [`export last detail: int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L122)
- **Ui.ClickEvent** · [`export let init = func(button: uint8, x: float64, y: float64, modifiers: Modifiers, detail: int32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L125)
- **Ui** · [`export let InputEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L129)
  Tagged `#Final`, `#Immutable`.
- **Ui.InputEvent** · [`export last value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L130)
- **Ui.InputEvent** · [`export last checked: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L131)
- **Ui.InputEvent** · [`export last composing: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L132)
- **Ui.InputEvent** · [`export let init = func(value: string, checked: bool, composing: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L135)
- **Ui** · [`export let ChangeEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L139)
  Tagged `#Final`, `#Immutable`.
- **Ui.ChangeEvent** · [`export last value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L140)
- **Ui.ChangeEvent** · [`export last checked: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L141)
- **Ui.ChangeEvent** · [`export let init = func(value: string, checked: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L144)
- **Ui** · [`export let SubmitEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L148)
  Tagged `#Final`, `#Immutable`.
- **Ui.SubmitEvent** · [`export last submitter: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L149)
- **Ui.SubmitEvent** · [`export let init = func(submitter: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L152)
- **Ui** · [`export let KeyEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L156)
  Tagged `#Final`, `#Immutable`.
- **Ui.KeyEvent** · [`export last key: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L157)
- **Ui.KeyEvent** · [`export last code: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L158)
- **Ui.KeyEvent** · [`export last modifiers: Modifiers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L159)
- **Ui.KeyEvent** · [`export last repeat: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L160)
- **Ui.KeyEvent** · [`export let init = func(key: string, code: string, modifiers: Modifiers, repeat: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L163)
- **Ui** · [`export let FocusEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L167)
  Tagged `#Final`, `#Immutable`.
- **Ui** · [`export let PointerEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L171)
  Tagged `#Final`, `#Immutable`.
- **Ui.PointerEvent** · [`export last x: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L172)
- **Ui.PointerEvent** · [`export last y: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L173)
- **Ui.PointerEvent** · [`export last button: uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L174)
- **Ui.PointerEvent** · [`export last kind: PointerKind`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L175)
- **Ui.PointerEvent** · [`export last modifiers: Modifiers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L176)
- **Ui.PointerEvent** · [`export let init = func(x: float64, y: float64, button: uint8, kind: PointerKind, modifiers: Modifiers)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L179)
- **Ui** · [`export let ScrollEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L183)
  Tagged `#Final`, `#Immutable`.
- **Ui.ScrollEvent** · [`export last top: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L184)
- **Ui.ScrollEvent** · [`export last left: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L185)
- **Ui.ScrollEvent** · [`export let init = func(top: float64, left: float64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L188)
- **Ui** · [`export let ToggleEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L192)
  Tagged `#Final`, `#Immutable`.
- **Ui.ToggleEvent** · [`export last open: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L193)
- **Ui.ToggleEvent** · [`export let init = func(open: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L196)
- **Ui** · [`export let CloseEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L200)
  Tagged `#Final`, `#Immutable`.
- **Ui.CloseEvent** · [`export last return_value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L201)
- **Ui.CloseEvent** · [`export let init = func(return_value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L204)
- **Ui** · [`export let Subscription = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L211)
  Stops one subscriber of a `State`. `close()` may be called any number of times. Tagged `#Final`, `#NotThreadSafe`.
- **Ui.Subscription** · [`export let close = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L215)
  Stops the notifications. Later calls do nothing.
- **Ui.Subscription** · [`export let is_open = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L220)
  Reports whether the subscriber is still notified.
- **Ui.Subscription** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L223)
  `Subscription(open)` or `Subscription(closed)`.
- **Ui** · [`export let State = class<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L251)
  One value of a deeply immutable type, observed by subscribers in one task. `T` must be deeply immutable (E0580). Each accepted `set` or `update` replaces the value at once and notifies every open subscriber once, in subscription order, with the new value, even when it equals the old one, unless the state was built with an `equal` function that reports the two equal. A `set` made while the state is notifying replaces the value and queues its notification, which runs when the current round ends; more than 100 rounds in one notification traps with `state update loop`. Tagged `#Final`, `#NotThreadSafe`.
- **Ui.State** · [`export let init = func(initial: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L260)
  A state holding `initial` that notifies on every accepted update.
- **Ui.State** · [`export let get = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L266)
  Returns the current value.
- **Ui.State** · [`export let set = func(next: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L269)
  Replaces the value and notifies, unless the `equal` function given to `with_equality` reports the old and new values equal.
- **Ui.State** · [`export let update = func(change: func(T) -> T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L297)
  Calls `change` once with the current value and sets its result. `change` is not retained.
- **Ui.State** · [`export let subscribe = func(listener: func(T) -> void) -> Subscription`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L302)
  Adds a subscriber that is notified after each accepted update until its subscription is closed.
- **Ui.State** · [`export let subscriber_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L309)
  The number of subscribers whose subscription is open.
- **Ui.State** · [`export let use_equality = func(same: func(T, T) -> bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L321)
  Suppresses the notification of an update whose value `same` reports equal to the current one. `same` is retained.
- **Ui.State** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L346)
  `State(` and the value's text `)`.
- **Ui** · [`export let with_equality = func<T>(initial: T, same: func(T, T) -> bool) -> State<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L350)
  A state holding `initial` whose updates notify only when `same(old, new)` is false. `same` is retained.
- **Ui** · [`export let Component = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L360)
  A state and the function that renders a view of its value, mounted at most once at a time. Build one with `component`. It renders when it is mounted and after each turn in which its state notified; unmounting closes its subscription and raises its generation. Tagged `#Final`, `#NotThreadSafe`.
- **Ui.Component** · [`export let subscription: Subscription = nil`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L364)
- **Ui.Component** · [`export let children: Component[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L366)
- **Ui.Component** · [`export let owner: Root = nil`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L368)
- **Ui.Component** · [`export let dirty: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L370)
- **Ui.Component** · [`export let view: View = nil`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L372)
- **Ui.Component** · [`export let mount_actions: (func(Root) -> void)[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L377)
  Called with the root each time the component is mounted, after it subscribes.
- **Ui.Component** · [`export let unmount_actions: (func() -> void)[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L380)
  Called each time the component is unmounted, after its children and before its generation rises.
- **Ui.Component** · [`export let init = func(render_view: func() -> View, attach: func(Component) -> Subscription)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L383)
- **Ui.Component** · [`export let generation = func -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L386)
  Raised by each unmount; a completion or event that carries an older generation is discarded.
- **Ui.Component** · [`export let is_mounted = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L389)
  Reports whether the component is part of a root.
- **Ui.Component** · [`export let render_count = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L392)
  The number of times the component has rendered.
- **Ui.Component** · [`export let current_view = func -> View`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L395)
  The view of the last render, or nil before the first.
- **Ui.Component** · [`export let invalidate = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L399)
  Marks the component for rendering at the end of the turn, when it is mounted.
- **Ui.Component** · [`export let render_now = func -> View`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L406)
- **Ui.Component** · [`export let attach_to = func(root: Root)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L416)
- **Ui.Component** · [`export let detach = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L428)
- **Ui.Component** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L448)
  `Component(generation N, mounted)` or `Component(generation N)`.
- **Ui** · [`export let component = func<T>(state: State<T>, render: func(T) -> View) -> Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L459)
  A component that renders `state`'s value with `render`. `render` is retained, so a capturing function is written `new func`. Rendering should only read and allocate; the compiler does not check that yet.
- **Ui** · [`export let slot = func(child: Component) -> View`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L468)
  A view that embeds `child`. The child renders by itself when its own state changes; the parent's render does not run again for it.
- **Ui** · [`export let DiagnosticKind = enum (ViewReused, DuplicateKey, LimitExceeded)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L471)
  Why a commit failed validation.
- **Ui** · [`export let Diagnostic = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L475)
  A commit the host refused without changing anything. Tagged `#Final`, `#Immutable`.
- **Ui.Diagnostic** · [`export last kind: DiagnosticKind`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L476)
- **Ui.Diagnostic** · [`export last message: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L477)
- **Ui.Diagnostic** · [`export last slot: uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L478)
- **Ui.Diagnostic** · [`export last index: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L479)
- **Ui.Diagnostic** · [`export last key: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L480)
- **Ui.Diagnostic** · [`export let init = func(kind: DiagnosticKind, message: string, slot: uint32, index: int64, key: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L482)
- **Ui.Diagnostic** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L485)
  The kind and the message.
- **Ui.Diagnostic** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L488)
  Another diagnostic with equal fields.
- **Ui.Diagnostic** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L498)
  Combines the hashes of the fields.
- **Ui** · [`export let RenderOutcome = enum (Committed, Invalid(Diagnostic))`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L503)
  What a host did with a commit: applied it, or refused it and kept the previous view.
- **Ui** · [`export let RenderFailure = enum (Invalid(Diagnostic), Fault(Error))`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L507)
  A failed commit, as given to a root's render-failure handler. `Fault` means the host failed while applying: the root stops taking events until `reset`.
- **Ui** · [`export let RootEvent = enum (InputOverflow(uint32))`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L510)
  A root-level event: events were dropped while the root was overloaded.
- **Ui** · [`export let Rendered = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L514)
  One component and the view it rendered in a turn. Tagged `#Final`, `#NotThreadSafe`.
- **Ui.Rendered** · [`export last component: Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L515)
- **Ui.Rendered** · [`export last view: View`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L516)
- **Ui.Rendered** · [`export let init = func(component: Component, view: View)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L519)
- **Ui.Rendered** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L522)
  `Rendered(` the component `)`.
- **Ui** · [`export let Commit = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L527)
  The renders of one turn, parents before children, sent to the host as one request. Tagged `#Final`, `#NotThreadSafe`.
- **Ui.Commit** · [`export last sequence: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L528)
- **Ui.Commit** · [`export last rendered: Rendered[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L529)
- **Ui.Commit** · [`export let init = func(sequence: uint64, rendered: Rendered[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L532)
- **Ui.Commit** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L535)
  `Commit(` the sequence number and the number of renders `)`.
- **Ui** · [`export let Host = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L541)
  The services a root needs from the document or window that shows it. The web, desktop and model hosts implement it; `mount` takes one.
- **Ui** · [`export let Launch = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L584)
  Starts one command run; its only use is to give a run's start function a type of its own. Tagged `#Final`, `#Immutable`.
- **Ui.Launch** · [`export last ticket: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L585)
- **Ui.Launch** · [`export let init = func(ticket: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L587)
- **Ui.Launch** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L590)
  `Launch(` the ticket `)`.
- **Ui** · [`export let Ticket = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L596)
  One command run registered with a root. Tagged `#Final`, `#NotThreadSafe`.
- **Ui.Ticket** · [`export last id: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L597)
- **Ui.Ticket** · [`export last launch: func(Launch, channels.Sender<uint64>) -> void`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L598)
- **Ui.Ticket** · [`export last finish: func(uint64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L599)
- **Ui.Ticket** · [`export last cancel: func() -> void`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L600)
- **Ui.Ticket** · [`export let stopped: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L601)
- **Ui.Ticket** · [`export let init = func(id: uint64, launch: func(Launch, channels.Sender<uint64>) -> void, finish: func(uint64) -> bool, cancel: func() -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L603)
- **Ui.Ticket** · [`export let stop = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L607)
  Asks the run to cancel, once.
- **Ui.Ticket** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L616)
  `Ticket(` the id `)`.
- **Ui** · [`export let RootStats = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L667)
  Counts that show what a root holds and has done. Tagged `#Final`, `#Immutable`.
- **Ui.RootStats** · [`export last components: int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L668)
- **Ui.RootStats** · [`export last subscriptions: int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L669)
- **Ui.RootStats** · [`export last queued: int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L670)
- **Ui.RootStats** · [`export last turns: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L671)
- **Ui.RootStats** · [`export last renders: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L672)
- **Ui.RootStats** · [`export last commits: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L673)
- **Ui.RootStats** · [`export last dropped: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L674)
- **Ui.RootStats** · [`export last discarded: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L675)
- **Ui.RootStats** · [`export let init = func(components: int32, subscriptions: int32, queued: int32, turns: uint64, renders: uint64, commits: uint64, dropped: uint64, discarded: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L678)
- **Ui.RootStats** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L682)
  Every count, by name.
- **Ui.RootStats** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L688)
  Other stats with equal counts.
- **Ui.RootStats** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L696)
  The hash of the counts' text.
- **Ui** · [`export let Root = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L703)
  A mounted component tree and its event loop. One turn takes the next event, calls its handler, renders each component whose state notified, once, parents before children, and commits the renders as one host request. Tagged `#Final`, `#NotThreadSafe`.
- **Ui.Root** · [`export let init = func(host: Host, handle: uint64, top: Component)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L735)
- **Ui.Root** · [`export let on_render_failure = func(handler: func(RenderFailure) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L746)
  Replaces the handler of failed commits. The default prints the failure and keeps the previous view.
- **Ui.Root** · [`export let on_overflow = func(handler: func(RootEvent) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L751)
  Sets the handler of `RootEvent.InputOverflow`, delivered before the first event accepted after an overflow.
- **Ui.Root** · [`export let set_queue_limit = func(limit: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L757)
  Sets the number of queued events at which the root is overloaded (U-7, default 1,024; at most 65,536).
- **Ui.Root** · [`export let host_handle = func -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L765)
  The host's handle of this root.
- **Ui.Root** · [`export let component = func -> Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L768)
  The component the root was mounted with.
- **Ui.Root** · [`export let is_faulted = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L771)
  Reports whether the root stopped after a host failure.
- **Ui.Root** · [`export let is_closed = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L774)
  Reports whether the root was closed.
- **Ui.Root** · [`export let is_overloaded = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L777)
  Reports whether events are being dropped because the queue is full.
- **Ui.Root** · [`export let post = func(listener: uint64, target: Component, coalesce: bool, deliver: func() -> void) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L783)
  Queues an event for `listener` of `target`, called by a host. A coalesced event replaces the queued one of the same listener and keeps its place. Over the queue limit an event that cannot be coalesced is dropped and counted, and false is returned. An event whose component unmounted before its turn is discarded.
- **Ui.Root** · [`export let turn = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L816)
  Runs one turn. Returns false when the root is closed or faulted, or the host has no more events.
- **Ui.Root** · [`export let run = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L862)
  Runs turns until the root is closed or faulted, or the host has no more events. A root whose components run commands is run by `Std.Ui.Commands.run` instead: a command started under `run` traps with `a command needs Std.Ui.Commands.run(root)`.
- **Ui.Root** · [`export let begin_commands = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L875)
  Marks the root as running commands; false when it already is.
- **Ui.Root** · [`export let next_launch = func -> Ticket`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L885)
  The next run to start under `Std.Ui.Commands.run`, taken off the list, or nil.
- **Ui.Root** · [`export let is_stopped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L896)
  Reports whether the loop of `Std.Ui.Commands.run` should stop: the root is closed or faulted.
- **Ui.Root** · [`export let has_queued = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L900)
  Reports whether an event is queued.
- **Ui.Root** · [`export let wait_for_events = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L904)
  Waits for host events with `wait`, or with `TaskHost.wait_with` when the host is one.
- **Ui.Root** · [`export let task_host = func -> TaskHost`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L908)
  The host as a `TaskHost`, or nil.
- **Ui.Root** · [`export let stop_commands = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L917)
  Asks every run in flight to cancel and forgets the runs not started.
- **Ui.Root** · [`export let end_commands = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L926)
  After every command task has finished: applies what the remaining runs reported, outside any turn.
- **Ui.Root** · [`export let set_command_limit = func(limit: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L939)
  Sets the number of commands in flight at which `Std.Ui.Commands.Command.run` is rejected (U-10, default 32; at most 1,024).
- **Ui.Root** · [`export let commands_in_flight = func -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L947)
  The number of command runs started and not yet finished, the stopped ones included.
- **Ui.Root** · [`export let stale_completions = func -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L950)
  How many command completions were discarded because their generation was old.
- **Ui.Root** · [`export let history = func -> HistoryHost`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L953)
  The host as a `HistoryHost`, or nil when it keeps no history.
- **Ui.Root** · [`export let on_navigation = func(listener: func(string) -> void) -> Subscription`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L962)
  Adds a listener for navigations the program did not make (back, forward, an edited address), which the host reports with `post_navigation`.
- **Ui.Root** · [`export let post_navigation = func(path: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L970)
  Queues a navigation to `path`, called by a host. A queued navigation is replaced by a later one, and navigations are never dropped by the queue limit.
- **Ui.Root** · [`export let request_focus = func(element_id: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L989)
  Asks the host to focus the element whose `id` is `element_id` once the current turn has committed. A later request in the same turn replaces it; a host that is not a `FocusHost` ignores it.
- **Ui.Root** · [`export let navigated = func(path: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L994)
- **Ui.Root** · [`export let can_launch = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1015)
  Reports whether another command may start: fewer than the limit are in flight.
- **Ui.Root** · [`export let launch = func(start: func(Launch, channels.Sender<uint64>) -> void, finish: func(uint64) -> bool, stop: func() -> void) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1020)
  Registers a command run; `launch` starts its task under `Std.Ui.Commands.run`, `finish` applies its completion in a turn, and `stop` asks it to cancel.
- **Ui.Root** · [`export let stop_ticket = func(id: uint64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1032)
  Asks the run `id` to cancel. A run not started yet is dropped, never completes, and true is returned.
- **Ui.Root** · [`export let complete = func(id: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1062)
  One turn: the completion of the run `id`.
- **Ui.Root** · [`export let reset = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1077)
  Clears a fault: the host empties the root, every component renders again and the root takes events.
- **Ui.Root** · [`export let close = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1088)
  Unmounts the component tree and closes the root on the host.
- **Ui.Root** · [`export let stats = func -> RootStats`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1099)
  The root's counts.
- **Ui.Root** · [`export let start = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1107)
- **Ui.Root** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1219)
  `Root(handle N)` with its state.
- **Ui** · [`export let Wake = enum (  Events,  Completed(uint64),   Idle )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1270)
  What a root's `TaskHost.wait_with` saw first.
- **Ui** · [`export let TaskHost = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1287)
  A host that can wait for its own events and for command completions together. A root run by `Std.Ui.Commands.run`, with commands in flight and no event queued, calls `wait_with` instead of `wait`. The host returns `Events` after posting events, or `Completed` with a ticket it received from `completions`, whichever comes first; or `Idle` when it will post nothing until a command completes. A host may open a scope of its own for the wait, for example to race a cancellable host request against the channel, as long as every task it starts has finished when `wait_with` returns.
- **Ui** · [`export let HistoryHost = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1294)
  A host that keeps the history of a document: the router reads and changes the location through it. Paths are absolute (`/parts/7`). The host reports a navigation the program did not make with `root.post_navigation(path)`.
- **Ui** · [`export let FocusHost = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1306)
  A host that can move the keyboard focus to an element of a root, named by its `id`.
- **Ui** · [`export let mount = func(host: Host, target: string, top: Component) -> result.Result<Root>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1314)
  Opens a root on `target` of `host`, renders `top` and commits it. `Err` when the host refuses the target (a second root on one element, more than U-9 roots) or `top` is mounted.
<!-- END SOURCE API -->

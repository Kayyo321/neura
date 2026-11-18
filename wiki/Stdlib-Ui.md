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
- **Ui.View** · [`export last last_slot: storage.Slot`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L22)
  The slot evaluated last; each slot's `next` is the one evaluated before it. Walk the slots in evaluation order with `Std.Ui.Internal.Build.slots_in_order`.
- **Ui.View** · [`export let owner: class = nil`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L25)
  The reconciler that last placed this view in a tree, and in which pass.
- **Ui.View** · [`export let pass: uint64 = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L27)
- **Ui.View** · [`export let init = func(descriptor: string, fragment: uint32, last_slot: storage.Slot)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L30)
- **Ui.View** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L32)
- **Ui** · [`export let Url = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L37)
  A URL checked for use in a template attribute. Tagged `#Final`, `#Immutable`.
- **Ui.Url** · [`export last text: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L39)
- **Ui.Url** · [`export let init = func(text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L42)
- **Ui** · [`export let CssValue = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L47)
  A numeric CSS custom property value and its unit. Tagged `#Final`, `#Immutable`.
- **Ui.CssValue** · [`export last value: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L49)
- **Ui.CssValue** · [`export last unit: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L51)
- **Ui.CssValue** · [`export let init = func(value: float64, unit: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L54)
- **Ui** · [`export let Stylesheet = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L62)
  A stylesheet that the build wrote beside the image, named by the SHA-256 of its text. The text is not in the image. The bundle links the file from `index.html`; holding the value keeps nothing alive and registers nothing at run time. Tagged `#Final`, `#Immutable`. Since 0.3.0.
- **Ui.Stylesheet** · [`export last digest: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L64)
  The lowercase hexadecimal SHA-256 of the written file.
- **Ui.Stylesheet** · [`export let init = func(digest: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L67)
- **Ui.Stylesheet** · [`export let path = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L70)
  The bundle path of the file, `styles/<digest>.css`.
- **Ui.Stylesheet** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L72)
- **Ui.Stylesheet** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L74)
- **Ui.Stylesheet** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L81)
- **Ui** · [`export let StyleModule = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L89)
  A CSS module the build wrote beside the image, with its class and keyframes names scoped. Use the constant's `class_name("name")` and `keyframes_name("name")`, which the compiler replaces with the scoped names; their argument is a string literal naming a declared export. Tagged `#Final`, `#Immutable`. Since 0.3.0.
- **Ui.StyleModule** · [`export last digest: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L91)
  The lowercase hexadecimal SHA-256 of the written file.
- **Ui.StyleModule** · [`export let init = func(digest: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L94)
- **Ui.StyleModule** · [`export let path = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L97)
  The bundle path of the file, `styles/<digest>.css`.
- **Ui.StyleModule** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L99)
- **Ui.StyleModule** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L101)
- **Ui.StyleModule** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L108)
- **Ui** · [`export let DiagnosticKind = enum (  ViewReused,  DuplicateKey,  LimitExceeded )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L113)
  Why a commit was refused before anything changed. Since 0.3.0.
- **Ui** · [`export let Diagnostic = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L125)
  A refused commit: what, which slot, and for a keyed item its position and key. Tagged `#Final`, `#Immutable`. Since 0.3.0.
- **Ui.Diagnostic** · [`export last kind: DiagnosticKind`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L126)
- **Ui.Diagnostic** · [`export last message: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L127)
- **Ui.Diagnostic** · [`export last slot: uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L129)
  The slot of the template whose value was refused.
- **Ui.Diagnostic** · [`export last index: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L131)
  The item's position in its `@for`, or -1.
- **Ui.Diagnostic** · [`export last key: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L133)
  The item's key as text, or empty.
- **Ui.Diagnostic** · [`export let init = func(kind: DiagnosticKind, message: string, slot: uint32, index: int64, key: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L136)
- **Ui.Diagnostic** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L139)
  The message, which names the refused rule.
- **Ui.Diagnostic** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L142)
  Another diagnostic with equal fields.
- **Ui.Diagnostic** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L152)
  Combines the hashes of the fields.
- **Ui** · [`export let RenderOutcome = enum (  Committed,  Invalid(Diagnostic) )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L158)
  The outcome of a commit that did not fail in the host. Since 0.3.0.
- **Ui** · [`export let url = func(text: string) -> Url`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L166)
  Returns a URL after checking that it is a reference or uses an allowed scheme.
- **Ui** · [`export let length = func(value: float64, unit: string) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L197)
  A CSS length with a supported unit.
- **Ui** · [`export let color = func(rgb: uint32) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L206)
  A CSS color from a 24-bit RGB value.
- **Ui** · [`export let number = func(value: float64) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L214)
  A unitless CSS number.
- **Ui** · [`export let percent = func(value: float64) -> CssValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L217)
  A CSS percentage.
- **Ui** · [`export let PointerKind = enum (Mouse, Pen, Touch, Other)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L220)
  The device that supplied a pointer event.
- **Ui** · [`export let Modifiers = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L223)
  Tagged `#Final`, `#Immutable`.
- **Ui.Modifiers** · [`export last alt: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L224)
- **Ui.Modifiers** · [`export last ctrl: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L225)
- **Ui.Modifiers** · [`export last meta: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L226)
- **Ui.Modifiers** · [`export last shift: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L227)
- **Ui.Modifiers** · [`export let init = func(alt: bool, ctrl: bool, meta: bool, shift: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L230)
- **Ui** · [`export let ClickEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L234)
  Tagged `#Final`, `#Immutable`.
- **Ui.ClickEvent** · [`export last button: uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L235)
- **Ui.ClickEvent** · [`export last x: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L236)
- **Ui.ClickEvent** · [`export last y: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L237)
- **Ui.ClickEvent** · [`export last modifiers: Modifiers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L238)
- **Ui.ClickEvent** · [`export last detail: int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L239)
- **Ui.ClickEvent** · [`export let init = func(button: uint8, x: float64, y: float64, modifiers: Modifiers, detail: int32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L242)
- **Ui** · [`export let InputEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L246)
  Tagged `#Final`, `#Immutable`.
- **Ui.InputEvent** · [`export last value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L247)
- **Ui.InputEvent** · [`export last checked: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L248)
- **Ui.InputEvent** · [`export last composing: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L249)
- **Ui.InputEvent** · [`export let init = func(value: string, checked: bool, composing: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L252)
- **Ui** · [`export let ChangeEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L256)
  Tagged `#Final`, `#Immutable`.
- **Ui.ChangeEvent** · [`export last value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L257)
- **Ui.ChangeEvent** · [`export last checked: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L258)
- **Ui.ChangeEvent** · [`export let init = func(value: string, checked: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L261)
- **Ui** · [`export let SubmitEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L265)
  Tagged `#Final`, `#Immutable`.
- **Ui.SubmitEvent** · [`export last submitter: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L266)
- **Ui.SubmitEvent** · [`export let init = func(submitter: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L269)
- **Ui** · [`export let KeyEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L273)
  Tagged `#Final`, `#Immutable`.
- **Ui.KeyEvent** · [`export last key: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L274)
- **Ui.KeyEvent** · [`export last code: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L275)
- **Ui.KeyEvent** · [`export last modifiers: Modifiers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L276)
- **Ui.KeyEvent** · [`export last repeat: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L277)
- **Ui.KeyEvent** · [`export let init = func(key: string, code: string, modifiers: Modifiers, repeat: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L280)
- **Ui** · [`export let FocusEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L284)
  Tagged `#Final`, `#Immutable`.
- **Ui** · [`export let PointerEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L288)
  Tagged `#Final`, `#Immutable`.
- **Ui.PointerEvent** · [`export last x: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L289)
- **Ui.PointerEvent** · [`export last y: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L290)
- **Ui.PointerEvent** · [`export last button: uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L291)
- **Ui.PointerEvent** · [`export last kind: PointerKind`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L292)
- **Ui.PointerEvent** · [`export last modifiers: Modifiers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L293)
- **Ui.PointerEvent** · [`export let init = func(x: float64, y: float64, button: uint8, kind: PointerKind, modifiers: Modifiers)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L296)
- **Ui** · [`export let ScrollEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L300)
  Tagged `#Final`, `#Immutable`.
- **Ui.ScrollEvent** · [`export last top: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L301)
- **Ui.ScrollEvent** · [`export last left: float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L302)
- **Ui.ScrollEvent** · [`export let init = func(top: float64, left: float64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L305)
- **Ui** · [`export let ToggleEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L309)
  Tagged `#Final`, `#Immutable`.
- **Ui.ToggleEvent** · [`export last open: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L310)
- **Ui.ToggleEvent** · [`export let init = func(open: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L313)
- **Ui** · [`export let CloseEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L317)
  Tagged `#Final`, `#Immutable`.
- **Ui.CloseEvent** · [`export last return_value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L318)
- **Ui.CloseEvent** · [`export let init = func(return_value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L321)
- **Ui** · [`export let Subscription = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L328)
  Stops one subscriber of a `State`. `close()` may be called any number of times. Tagged `#Final`, `#NotThreadSafe`.
- **Ui.Subscription** · [`export let close = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L332)
  Stops the notifications. Later calls do nothing.
- **Ui.Subscription** · [`export let is_open = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L337)
  Reports whether the subscriber is still notified.
- **Ui.Subscription** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L340)
  `Subscription(open)` or `Subscription(closed)`.
- **Ui** · [`export let State = class<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L368)
  One value of a deeply immutable type, observed by subscribers in one task. `T` must be deeply immutable (E0580). Each accepted `set` or `update` replaces the value at once and notifies every open subscriber once, in subscription order, with the new value, even when it equals the old one, unless the state was built with an `equal` function that reports the two equal. A `set` made while the state is notifying replaces the value and queues its notification, which runs when the current round ends; more than 100 rounds in one notification traps with `state update loop`. Tagged `#Final`, `#NotThreadSafe`.
- **Ui.State** · [`export let init = func(initial: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L377)
  A state holding `initial` that notifies on every accepted update.
- **Ui.State** · [`export let get = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L383)
  Returns the current value.
- **Ui.State** · [`export let set = func(next: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L386)
  Replaces the value and notifies, unless the `equal` function given to `with_equality` reports the old and new values equal.
- **Ui.State** · [`export let update = func(change: func(T) -> T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L414)
  Calls `change` once with the current value and sets its result. `change` is not retained.
- **Ui.State** · [`export let subscribe = func(listener: func(T) -> void) -> Subscription`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L419)
  Adds a subscriber that is notified after each accepted update until its subscription is closed.
- **Ui.State** · [`export let subscriber_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L426)
  The number of subscribers whose subscription is open.
- **Ui.State** · [`export let use_equality = func(same: func(T, T) -> bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L438)
  Suppresses the notification of an update whose value `same` reports equal to the current one. `same` is retained.
- **Ui.State** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L463)
  `State(` and the value's text `)`.
- **Ui** · [`export let with_equality = func<T>(initial: T, same: func(T, T) -> bool) -> State<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L467)
  A state holding `initial` whose updates notify only when `same(old, new)` is false. `same` is retained.
- **Ui** · [`export let Component = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L477)
  A state and the function that renders a view of its value, mounted at most once at a time. Build one with `component`. It renders when it is mounted and after each turn in which its state notified; unmounting closes its subscription and raises its generation. Tagged `#Final`, `#NotThreadSafe`.
- **Ui.Component** · [`export let subscription: Subscription = nil`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L481)
- **Ui.Component** · [`export let children: Component[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L483)
- **Ui.Component** · [`export let owner: Root = nil`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L485)
- **Ui.Component** · [`export let dirty: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L487)
- **Ui.Component** · [`export let view: View = nil`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L489)
- **Ui.Component** · [`export let reconciled: class = nil`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L493)
  What the root's reconciler keeps of the component's mounted nodes, so that a commit in which neither the component nor a component below it rendered reuses them (`Std.Ui.Internal.Reconcile`).
- **Ui.Component** · [`export let rendered_by: class = nil`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L496)
  The reconciler and pass of the last commit for which the component rendered.
- **Ui.Component** · [`export let rendered_pass: uint64 = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L498)
- **Ui.Component** · [`export let mount_actions: (func(Root) -> void)[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L503)
  Called with the root each time the component is mounted, after it subscribes.
- **Ui.Component** · [`export let unmount_actions: (func() -> void)[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L506)
  Called each time the component is unmounted, after its children and before its generation rises.
- **Ui.Component** · [`export let init = func(render_view: func() -> View, attach: func(Component) -> Subscription)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L509)
- **Ui.Component** · [`export let generation = func -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L512)
  Raised by each unmount; a completion or event that carries an older generation is discarded.
- **Ui.Component** · [`export let is_mounted = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L515)
  Reports whether the component is part of a root.
- **Ui.Component** · [`export let render_count = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L518)
  The number of times the component has rendered.
- **Ui.Component** · [`export let current_view = func -> View`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L521)
  The view of the last render, or nil before the first.
- **Ui.Component** · [`export let invalidate = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L525)
  Marks the component for rendering at the end of the turn, when it is mounted.
- **Ui.Component** · [`export let render_now = func -> View`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L533)
- **Ui.Component** · [`export let attach_to = func(root: Root)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L543)
- **Ui.Component** · [`export let detach = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L555)
- **Ui.Component** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L577)
  `Component(generation N, mounted)` or `Component(generation N)`.
- **Ui** · [`export let component = func<T>(state: State<T>, render: func(T) -> View) -> Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L588)
  A component that renders `state`'s value with `render`. `render` is retained, so a capturing function is written `new func`. Rendering should only read and allocate; the compiler does not check that yet.
- **Ui** · [`export let slot = func(child: Component) -> View`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L597)
  A view that embeds `child`. The child renders by itself when its own state changes; the parent's render does not run again for it.
- **Ui** · [`export let RenderFailure = enum (Invalid(Diagnostic), Fault(Error))`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L601)
  A failed commit, as given to a root's render-failure handler. `Fault` means the host failed while applying: the root stops taking events until `reset`.
- **Ui** · [`export let RootEvent = enum (InputOverflow(uint32))`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L604)
  A root-level event: events were dropped while the root was overloaded.
- **Ui** · [`export let Rendered = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L608)
  One component and the view it rendered in a turn. Tagged `#Final`, `#NotThreadSafe`.
- **Ui.Rendered** · [`export last component: Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L609)
- **Ui.Rendered** · [`export last view: View`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L610)
- **Ui.Rendered** · [`export let init = func(component: Component, view: View)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L613)
- **Ui.Rendered** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L616)
  `Rendered(` the component `)`.
- **Ui** · [`export let Commit = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L622)
  The renders of one turn, parents before children, sent to the host as one request. A host that reconciles builds the root's tree from `top`: the components that did not render keep their views. Tagged `#Final`, `#NotThreadSafe`.
- **Ui.Commit** · [`export last sequence: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L623)
- **Ui.Commit** · [`export last rendered: Rendered[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L624)
- **Ui.Commit** · [`export last top: Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L626)
  The component the root was mounted with.
- **Ui.Commit** · [`export let init = func(sequence: uint64, rendered: Rendered[list], top: Component)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L629)
- **Ui.Commit** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L632)
  `Commit(` the sequence number and the number of renders `)`.
- **Ui** · [`export let Host = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L638)
  The services a root needs from the document or window that shows it. The web, desktop and model hosts implement it; `mount` takes one.
- **Ui** · [`export let HostStats = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L658)
  What a host holds for one root (budget B-W9): its nodes (the container excluded), installed listeners, registered templates and queued events. A host that keeps no document reports zeros. Tagged `#Final`, `#Immutable`.
- **Ui.HostStats** · [`export last nodes: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L659)
- **Ui.HostStats** · [`export last listeners: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L660)
- **Ui.HostStats** · [`export last templates: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L661)
- **Ui.HostStats** · [`export last queued: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L662)
- **Ui.HostStats** · [`export let init = func(nodes: int64, listeners: int64, templates: int64, queued: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L664)
- **Ui.HostStats** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L667)
  Every count, by name.
- **Ui.HostStats** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L672)
  Other stats with equal counts.
- **Ui.HostStats** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L682)
  Combines the hashes of the counts.
- **Ui** · [`export let Launch = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L714)
  Starts one command run; its only use is to give a run's start function a type of its own. Tagged `#Final`, `#Immutable`.
- **Ui.Launch** · [`export last ticket: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L715)
- **Ui.Launch** · [`export let init = func(ticket: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L717)
- **Ui.Launch** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L720)
  `Launch(` the ticket `)`.
- **Ui** · [`export let Ticket = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L726)
  One command run registered with a root. Tagged `#Final`, `#NotThreadSafe`.
- **Ui.Ticket** · [`export last id: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L727)
- **Ui.Ticket** · [`export last launch: func(Launch, channels.Sender<uint64>) -> void`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L728)
- **Ui.Ticket** · [`export last finish: func(uint64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L729)
- **Ui.Ticket** · [`export last cancel: func() -> void`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L730)
- **Ui.Ticket** · [`export let stopped: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L731)
- **Ui.Ticket** · [`export let init = func(id: uint64, launch: func(Launch, channels.Sender<uint64>) -> void, finish: func(uint64) -> bool, cancel: func() -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L733)
- **Ui.Ticket** · [`export let stop = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L737)
  Asks the run to cancel, once.
- **Ui.Ticket** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L746)
  `Ticket(` the id `)`.
- **Ui** · [`export let RootStats = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L797)
  Counts that show what a root holds and has done. Tagged `#Final`, `#Immutable`.
- **Ui.RootStats** · [`export last components: int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L798)
- **Ui.RootStats** · [`export last subscriptions: int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L799)
- **Ui.RootStats** · [`export last queued: int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L800)
- **Ui.RootStats** · [`export last turns: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L801)
- **Ui.RootStats** · [`export last renders: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L802)
- **Ui.RootStats** · [`export last commits: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L803)
- **Ui.RootStats** · [`export last dropped: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L804)
- **Ui.RootStats** · [`export last discarded: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L805)
- **Ui.RootStats** · [`export let init = func(components: int32, subscriptions: int32, queued: int32, turns: uint64, renders: uint64, commits: uint64, dropped: uint64, discarded: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L808)
- **Ui.RootStats** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L812)
  Every count, by name.
- **Ui.RootStats** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L818)
  Other stats with equal counts.
- **Ui.RootStats** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L826)
  The hash of the counts' text.
- **Ui** · [`export let Root = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L833)
  A mounted component tree and its event loop. One turn takes the next event, calls its handler, renders each component whose state notified, once, parents before children, and commits the renders as one host request. Tagged `#Final`, `#NotThreadSafe`.
- **Ui.Root** · [`export let init = func(host: Host, handle: uint64, top: Component)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L871)
- **Ui.Root** · [`export let on_render_failure = func(handler: func(RenderFailure) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L882)
  Replaces the handler of failed commits. The default prints the failure and keeps the previous view.
- **Ui.Root** · [`export let on_overflow = func(handler: func(RootEvent) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L887)
  Sets the handler of `RootEvent.InputOverflow`, delivered before the first event accepted after an overflow.
- **Ui.Root** · [`export let set_queue_limit = func(limit: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L893)
  Sets the number of queued events at which the root is overloaded (U-7, default 1,024; at most 65,536).
- **Ui.Root** · [`export let host_handle = func -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L901)
  The host's handle of this root.
- **Ui.Root** · [`export let component = func -> Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L904)
  The component the root was mounted with.
- **Ui.Root** · [`export let is_faulted = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L907)
  Reports whether the root stopped after a host failure.
- **Ui.Root** · [`export let is_closed = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L910)
  Reports whether the root was closed.
- **Ui.Root** · [`export let is_overloaded = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L913)
  Reports whether events are being dropped because the queue is full.
- **Ui.Root** · [`export let post = func(listener: uint64, target: Component, coalesce: bool, deliver: func() -> void) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L919)
  Queues an event for `listener` of `target`, called by a host. A coalesced event replaces the queued one of the same listener and keeps its place. Over the queue limit an event that cannot be coalesced is dropped and counted, and false is returned. An event whose component unmounted before its turn is discarded.
- **Ui.Root** · [`export let post_overflow = func(dropped: uint32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L954)
  Queues the notice that the host dropped `dropped` events while the root was overloaded. It is delivered to `on_overflow` before the events posted after it. Called by a host that queues events itself.
- **Ui.Root** · [`export let host_stats = func -> HostStats`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L963)
  The host's counts for this root: nodes, listeners, templates and queued events.
- **Ui.Root** · [`export let turn = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L966)
  Runs one turn. Returns false when the root is closed or faulted, or the host has no more events.
- **Ui.Root** · [`export let run = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1012)
  Runs turns until the root is closed or faulted, or the host has no more events. A root whose components run commands is run by `Std.Ui.Commands.run` instead: a command started under `run` traps with `a command needs Std.Ui.Commands.run(root)`.
- **Ui.Root** · [`export let begin_commands = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1025)
  Marks the root as running commands; false when it already is.
- **Ui.Root** · [`export let next_launch = func -> Ticket`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1035)
  The next run to start under `Std.Ui.Commands.run`, taken off the list, or nil.
- **Ui.Root** · [`export let is_stopped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1046)
  Reports whether the loop of `Std.Ui.Commands.run` should stop: the root is closed or faulted.
- **Ui.Root** · [`export let has_queued = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1050)
  Reports whether an event is queued.
- **Ui.Root** · [`export let wait_for_events = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1054)
  Waits for host events with `wait`, or with `TaskHost.wait_with` when the host is one.
- **Ui.Root** · [`export let task_host = func -> TaskHost`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1058)
  The host as a `TaskHost`, or nil.
- **Ui.Root** · [`export let stop_commands = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1067)
  Asks every run in flight to cancel and forgets the runs not started.
- **Ui.Root** · [`export let end_commands = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1076)
  After every command task has finished: applies what the remaining runs reported, outside any turn.
- **Ui.Root** · [`export let set_command_limit = func(limit: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1089)
  Sets the number of commands in flight at which `Std.Ui.Commands.Command.run` is rejected (U-10, default 32; at most 1,024).
- **Ui.Root** · [`export let commands_in_flight = func -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1097)
  The number of command runs started and not yet finished, the stopped ones included.
- **Ui.Root** · [`export let stale_completions = func -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1100)
  How many command completions were discarded because their generation was old.
- **Ui.Root** · [`export let history = func -> HistoryHost`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1103)
  The host as a `HistoryHost`, or nil when it keeps no history.
- **Ui.Root** · [`export let on_navigation = func(listener: func(string) -> void) -> Subscription`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1112)
  Adds a listener for navigations the program did not make (back, forward, an edited address), which the host reports with `post_navigation`.
- **Ui.Root** · [`export let post_navigation = func(path: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1120)
  Queues a navigation to `path`, called by a host. A queued navigation is replaced by a later one, and navigations are never dropped by the queue limit.
- **Ui.Root** · [`export let listens_for_navigation = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1140)
  Reports whether a listener added with `on_navigation` is still open, so that a host waits for the document's navigations only for a root that uses them.
- **Ui.Root** · [`export let invalidated = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1151)
  Called by `Component.invalidate`.
- **Ui.Root** · [`export let render_all_again = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1161)
  Renders every component again, as after a reset. Called during a render, the components render again before the turn commits, so the page never shows the first renders. The router calls it when it learns, as it mounts, that links rendered earlier in the turn used another routing form.
- **Ui.Root** · [`export let request_focus = func(element_id: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1170)
  Asks the host to focus the element whose `id` is `element_id` once the current turn has committed. A later request in the same turn replaces it; a host that is not a `FocusHost` ignores it.
- **Ui.Root** · [`export let navigated = func(path: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1175)
- **Ui.Root** · [`export let can_launch = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1196)
  Reports whether another command may start: fewer than the limit are in flight.
- **Ui.Root** · [`export let launch = func(start: func(Launch, channels.Sender<uint64>) -> void, finish: func(uint64) -> bool, stop: func() -> void) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1201)
  Registers a command run; `launch` starts its task under `Std.Ui.Commands.run`, `finish` applies its completion in a turn, and `stop` asks it to cancel.
- **Ui.Root** · [`export let stop_ticket = func(id: uint64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1213)
  Asks the run `id` to cancel. A run not started yet is dropped, never completes, and true is returned.
- **Ui.Root** · [`export let complete = func(id: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1243)
  One turn: the completion of the run `id`.
- **Ui.Root** · [`export let reset = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1257)
  Clears a fault: the host empties the root, every component renders again and the root takes events.
- **Ui.Root** · [`export let close = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1268)
  Unmounts the component tree and closes the root on the host.
- **Ui.Root** · [`export let stats = func -> RootStats`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1279)
  The root's counts.
- **Ui.Root** · [`export let start = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1287)
- **Ui.Root** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1435)
  `Root(handle N)` with its state.
- **Ui** · [`export let Wake = enum (  Events,  Completed(uint64),   Idle )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1482)
  What a root's `TaskHost.wait_with` saw first.
- **Ui** · [`export let TaskHost = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1499)
  A host that can wait for its own events and for command completions together. A root run by `Std.Ui.Commands.run`, with commands in flight and no event queued, calls `wait_with` instead of `wait`. The host returns `Events` after posting events, or `Completed` with a ticket it received from `completions`, whichever comes first; or `Idle` when it will post nothing until a command completes. A host may open a scope of its own for the wait, for example to race a cancellable host request against the channel, as long as every task it starts has finished when `wait_with` returns.
- **Ui** · [`export let HistoryHost = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1506)
  A host that keeps the history of a document: the router reads and changes the location through it. Paths are absolute (`/parts/7`). The host reports a navigation the program did not make with `root.post_navigation(path)`.
- **Ui** · [`export let FocusHost = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1518)
  A host that can move the keyboard focus to an element of a root, named by its `id`.
- **Ui** · [`export let mount = func(host: Host, target: string, top: Component) -> result.Result<Root>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui.nra#L1525)
  Opens a root on `target` of `host`, renders `top` and commits it. `Err` when the host refuses the target (a second root on one element, more than U-9 roots) or `top` is mounted.
<!-- END SOURCE API -->

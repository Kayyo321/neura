# `Std.Ui.Controls`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Ui.Controls`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Ui.Controls`](#exported-declarations)
- [State and components](https://github.com/Kayyo321/neura/blob/main/docs/apps/Components.md#forms-and-controls)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Controls** · [`export let Choice = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L16)
  One option of a `select`: the value submitted and the text shown. Tagged `#Final`, `#Immutable`.
- **Controls.Choice** · [`export last value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L17)
- **Controls.Choice** · [`export last label: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L18)
- **Controls.Choice** · [`export let init = func(value: string, label: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L20)
- **Controls.Choice** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L23)
  `value: label`.
- **Controls.Choice** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L26)
  Another choice with the same value and label.
- **Controls.Choice** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L34)
  Combines the hashes of the value and label.
- **Controls** · [`export let TableRow = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L39)
  One row of a `table`: a key that stays with the row when rows move, and its cells' text. Tagged `#Final`, `#Immutable`.
- **Controls.TableRow** · [`export last key: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L40)
- **Controls.TableRow** · [`export last cells: frozen.FrozenList<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L41)
- **Controls.TableRow** · [`export let init = func(key: string, cells: frozen.FrozenList<string>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L43)
- **Controls.TableRow** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L46)
  `TableRow(` the key `)`.
- **Controls.TableRow** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L49)
  Another row with the same key and cells.
- **Controls.TableRow** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L57)
  Combines the hashes of the key and cells.
- **Controls** · [`export let text_input = func<T>(field: forms.Field<T>, kind: string) -> ui.Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L160)
  A labelled `input` of the type `kind` (`text`, `email`, `password`, `search`, `tel`, `url` or `number`) over `field`: typing sets the draft, leaving the input touches the field, and a touched invalid field shows its message in an alert linked by `aria-describedby`.
- **Controls** · [`export let text_area = func<T>(field: forms.Field<T>) -> ui.Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L175)
  A labelled `textarea` over `field`, as `text_input`.
- **Controls** · [`export let checkbox = func(id: string, label: string, state: ui.State<bool>) -> ui.Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L186)
  A labelled checkbox over `state`: a change sets the state to the box's `checked`.
- **Controls** · [`export let select = func<T>(field: forms.Field<T>, choices: frozen.FrozenList<Choice>) -> ui.Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L195)
  A labelled `select` of `choices` over `field`, whose draft is the chosen value; a change sets it and touches the field.
- **Controls** · [`export let button = func(label: string, on_click: func(ui.ClickEvent) -> void) -> ui.View`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L204)
  A `button` of type `button` that calls `on_click`.
- **Controls** · [`export let submit_button = func(label: string) -> ui.View`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L208)
  A `button` of type `submit`, which submits the form it is in.
- **Controls** · [`export let form = func(form: forms.Form, action: func() -> void, content: func(forms.FormState) -> ui.View) -> ui.Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L214)
  A `form` element over `form`, rendering `content` with the form's state. Submitting it (the Enter key or a submit button) validates every field without a page load and, when all are valid, calls `action`; otherwise the first invalid field gets the focus. `content` and `action` are retained.
- **Controls** · [`export let list = func<T>(items: ui.State<frozen.FrozenList<T>>, key: func(T) -> string, item: func(T) -> ui.View) -> ui.Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L225)
  A `ul` with one `li` per item of `items`, keyed by `key` so that an item keeps its element when items move. `key` and `item` are retained; `item` renders one item.
- **Controls** · [`export let table = func(caption: string, columns: frozen.FrozenList<string>, rows: ui.State<frozen.FrozenList<TableRow>>) -> ui.Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L242)
  A `table` with a caption, a header row of `columns` and one row per `TableRow` of `rows`, keyed by the row's key; each row has one cell per column, and a row with another number of cells traps when it renders. Two columns with one name are refused by the host (a duplicate key).
- **Controls** · [`export let dialog = func(id: string, title: string, open: ui.State<bool>, content: func() -> ui.View) -> ui.Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/controls.nra#L263)
  A non-modal `dialog` whose `open` state is `open`, titled `title`, with the view `content` renders and a Close button. Closing it (the button, or the browser's own close) sets `open` to false. `content` is retained.
<!-- END SOURCE API -->

## Detailed contracts

- Text inputs, text areas, selects and checkboxes are components over a field's or a state's `ui.State`; lists, tables and dialogs are components over their state; buttons are views. Each uses the platform's element and renders alone when its own state changes.
- The markup is plain and unstyled. A view given to a control comes from a render function, because a control renders a fresh view each time.
- Task safety: the controls belong to the task that made them, like every `Std.Ui` value.

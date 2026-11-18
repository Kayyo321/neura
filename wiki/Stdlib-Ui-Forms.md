# `Std.Ui.Forms`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Ui.Forms`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Ui.Forms`](#exported-declarations)
- [State and components](https://github.com/Kayyo321/neura/blob/main/docs/apps/Components.md#forms-and-controls)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Forms** · [`export let FieldState = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L15)
  What a field shows: the draft, whether the person has left it or submitted the form (`touched`), and the parse error's message when the draft is not valid. Tagged `#Final`, `#Immutable`.
- **Forms.FieldState** · [`export last draft: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L16)
- **Forms.FieldState** · [`export last touched: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L17)
- **Forms.FieldState** · [`export last valid: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L18)
- **Forms.FieldState** · [`export last message: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L19)
- **Forms.FieldState** · [`export let init = func(draft: string, touched: bool, valid: bool, message: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L22)
- **Forms.FieldState** · [`export let shows_error = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L25)
  Reports whether the field shows its error: it is touched and not valid.
- **Forms.FieldState** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L28)
  `FieldState(` the draft, and `touched` and the message when they apply `)`.
- **Forms.FieldState** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L40)
  Another state with equal fields.
- **Forms.FieldState** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L48)
  Combines the hashes of the fields.
- **Forms** · [`export let Validated = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L53)
  What a form needs from each of its fields, whatever its value type.
- **Forms** · [`export let Field = class<T> implements Validated`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L66)
  One input: an element `id`, a label, the draft in a `ui.State<FieldState>`, and `parse`, which turns the draft into a `T` or an error whose message the field shows. `parse` runs on every change of the draft and must not keep state. Tagged `#Final`, `#NotThreadSafe`.
- **Forms.Field** · [`export last state: ui.State<FieldState>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L72)
  The field's state, which its control renders.
- **Forms.Field** · [`export let init = func(id: string, label: string, initial: string, parse: func(string) -> result.Result<T>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L76)
  A field whose element has the `id` `id` (letters, digits, `-` and `_`), labelled `label`, starting with the draft `initial`. `parse` is retained.
- **Forms.Field** · [`export let element_id = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L86)
- **Forms.Field** · [`export let label = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L89)
  The label text.
- **Forms.Field** · [`export let draft = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L92)
  The current draft.
- **Forms.Field** · [`export let set_draft = func(text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L95)
  Replaces the draft, as typing does; the field keeps whether it was touched.
- **Forms.Field** · [`export let touch = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L100)
  Marks the field touched, as leaving it does, so that it shows its error.
- **Forms.Field** · [`export let value = func -> result.Result<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L107)
  The draft parsed: `Ok` with the value, or the parse error.
- **Forms.Field** · [`export let validate = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L112)
- **Forms.Field** · [`export let reset = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L117)
- **Forms.Field** · [`export let on_input = func -> func(ui.InputEvent) -> void`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L122)
  A handler for the element's `input` event: it sets the draft to the element's value.
- **Forms.Field** · [`export let on_change = func -> func(ui.ChangeEvent) -> void`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L130)
  A handler for the element's `change` event (a `select`): it sets the draft and touches the field.
- **Forms.Field** · [`export let on_blur = func -> func(ui.FocusEvent) -> void`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L139)
  A handler for the element's `blur` event: it touches the field.
- **Forms.Field** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L159)
  `Field(` the id and the state `)`.
- **Forms** · [`export let FormState = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L165)
  What a form has done: the submits attempted, those that were valid, and the `id` of the first invalid field of the last attempt (empty when it was valid or none was made). Tagged `#Final`, `#Immutable`.
- **Forms.FormState** · [`export last attempts: int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L166)
- **Forms.FormState** · [`export last submitted: int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L167)
- **Forms.FormState** · [`export last first_invalid: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L168)
- **Forms.FormState** · [`export let init = func(attempts: int32, submitted: int32, first_invalid: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L171)
- **Forms.FormState** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L174)
  `FormState(` the counts and the first invalid field `)`.
- **Forms.FormState** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L179)
  Another state with equal fields.
- **Forms.FormState** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L187)
  Combines the hashes of the fields.
- **Forms** · [`export let Form = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L195)
  Fields that are submitted together. `submit` validates every field, so each shows its error, and either calls the submit action or asks the root of the form's component to focus the first invalid field after the turn's commit. Tagged `#Final`, `#NotThreadSafe`.
- **Forms.Form** · [`export last state: ui.State<FormState> = new ui.State<FormState>(new FormState(0, 0, ""))`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L199)
  What the form has done.
- **Forms.Form** · [`export let add = func(field: Validated)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L202)
  Adds a field, after the ones added before; the first invalid field in this order gets the focus.
- **Forms.Form** · [`export let bind = func(component: ui.Component)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L207)
  The component whose root moves the focus. `Std.Ui.Controls.form` binds it.
- **Forms.Form** · [`export let submit = func(action: func() -> void) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L214)
  Validates every field. When all are valid it records a submit and calls `action`; otherwise it records the first invalid field and asks for the focus to move there. Returns whether the form was valid.
- **Forms.Form** · [`export let on_submit = func(action: func() -> void) -> func(ui.SubmitEvent) -> void`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L236)
  A handler for the form element's `submit` event, bound with `on:submit.prevent_default`. `action` is retained and runs when the form is valid.
- **Forms.Form** · [`export let reset = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L244)
  Resets every field and the counts.
- **Forms.Form** · [`export let field_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L252)
  The number of fields.
- **Forms.Form** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L255)
  `Form(` the number of fields and the state `)`.
- **Forms** · [`export let any_text = func -> func(string) -> result.Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L260)
  A parser that accepts any text as it is.
- **Forms** · [`export let required_text = func(message: string) -> func(string) -> result.Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L264)
  A parser that accepts text with something other than spaces, trimmed; otherwise `message`.
- **Forms** · [`export let integer = func(min: int64, max: int64, message: string) -> func(string) -> result.Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L275)
  A parser of a whole decimal number from `min` to `max`, with optional spaces around it; otherwise `message`.
- **Forms** · [`export let email = func(message: string) -> func(string) -> result.Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/forms.nra#L315)
  A parser of an e-mail address by its shape only: one `@` with text before it, a domain after it with a dot inside, and no spaces; trimmed. Otherwise `message`. Whether the address exists is for the server to find out.
<!-- END SOURCE API -->

## Detailed contracts

- `Field<T>` keeps a draft, whether it is touched, and the parse error's message in a `ui.State<FieldState>`; `Form` validates every field on submit and asks the root to focus the first invalid one.
- The parsers `any_text`, `required_text`, `integer` and `email` are ordinary functions; `email` checks the shape of an address only.
- Task safety: fields and forms belong to the task that made them.

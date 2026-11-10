# `Std.Program`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Program`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Program`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Program** · [`export let ExitCode = enum (Ok, Failure, Status(int))`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L10)
  Ok is status 0, Failure is 1, and Status gives 0 through 255; any other Status traps with "invalid argument: exit status".
- **Program** · [`export let Entry = module`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L12)
- **Program.Entry** · [`export let main = func(args: string[]) -> ExitCode`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L13)
- **Program** · [`export let Environment = module`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L16)
- **Program.Environment** · [`export let Lookup = enum (Found(string), Missing, Failed(Error))`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L17)
- **Program.Environment** · [`export let get = func(name: string) -> Lookup`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L24)
  The value of an environment variable. Needs the env capability; without it the lookup fails with CapabilityDenied.
- **Program** · [`export let Process = module`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L33)
- **Program.Process** · [`export let exit = func(status: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L39)
  Ends the run with `status` (0 through 255) and does not return. Returning from main is preferred.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 11.3 `Std.Program`

This module replaces `SysIo.Entry` ([section 4.6](Stdlib-Design#46-retiring-sysio-and-test)). It needs P3.

- **`Entry`** is a module with one requirement, `main(args: string[]) → ExitCode`. A program's entry module inherits it and overrides `main`, exactly as programs inherit `SysIo.Entry` today. Program arguments arrive only through this parameter.
- **`ExitCode`** is a payload enum: `Ok`, `Failure`, and `Status(int)`. They map to process statuses 0, 1, and the given value. A `Status` outside 0 through 255 traps with `invalid argument: exit status`. The first plan kept the old one-variant enum; a payload enum lets a program return any status without a host call.
- **`Environment.get(name: string) → Lookup`**, where `Lookup` is a payload enum: `Found(string)`, `Missing`, and `Failed(Error)`. A purpose-built enum reads better here than a `Result` of an `Optional`. Needs P2.
- **`Process.exit(status: int)`** ends the run with that status through `neura.process.exit` and does not return. A status outside 0 through 255 traps. Needs P2. Returning from `main` is preferred; `exit` is for leaving from deep inside a program.

### 18.2 Program, console, and testing

- **Entry.** `Std.Program` holds `ExitCode`, `Entry`, `Environment`, and `Process` as one module. A `Status` outside 0 through 255 traps with `invalid argument: exit status`.
- **Process exit.** `Process.exit` is available without a capability. A test that exits with a nonzero status fails.
- **Std.Test messages.** `assert_equal_float64` compares with float `equals` (NaN equals NaN, `0.0` differs from `-0.0`). `assert_none` and `assert_err` report `expected None, got Some` and `expected Err, got Ok` without the value's text, so they work for payload enums. `assert_not_equal` reports `expected a value other than <x>`.
- **Console.** Output is unbuffered. Input is read in 4,096-byte requests shared by `read_line` and `input()`. A write that makes no progress is an `Other` error.

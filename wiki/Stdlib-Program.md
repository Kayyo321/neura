# `Std.Program`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Program`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Program`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Program** · [`export let ExitCode = enum ( Ok, Failure, Status(int), SeriousFailure, CommandNotFound, IllegalInstruction, Breakpoint, Abort, ArithmeticError, OutOfMemory, BusError, SegmentationFault, CpuTimeExceeded, FileSizeExceeded )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L18)
  A process exit status. Ok is 0, Failure is 1, and Status gives 0 through 255; any other Status traps with "invalid argument: exit status". The named variants follow the shell convention of 128 plus the Linux signal number for a process ended by that signal. SeriousFailure is 2, CommandNotFound 127, IllegalInstruction 132 (SIGILL), Breakpoint 133 (SIGTRAP), Abort 134 (SIGABRT), ArithmeticError 136 (SIGFPE, including integer division by zero), OutOfMemory 137 (SIGKILL, as sent by an out-of-memory killer), BusError 138 (SIGBUS), SegmentationFault 139 (SIGSEGV), CpuTimeExceeded 152 (SIGXCPU), and FileSizeExceeded 153 (SIGXFSZ). Platforms that number SIGXCPU and SIGXFSZ 30 and 31, such as MIPS, report 158 and 159 for those signals instead. A runtime trap ends the run with the status of the variant that names its class.
- **Program** · [`export let status_of = func(code: ExitCode) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L38)
  The process status `code` stands for: 0 through 255. Traps with "invalid argument: exit status" for a Status outside that range. Tagged `#NoDiscard`, `#Pure`.
- **Program** · [`export let Entry = module`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L66)
  The module contract implemented by a runnable program.
- **Program.Entry** · [`export let main = func(args: string[]) -> ExitCode`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L68)
  Runs the program with its command-line arguments and returns its exit status.
- **Program** · [`export let Environment = module`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L72)
  Looks up process environment variables through the host.
- **Program.Environment** · [`export let Lookup = enum (Found(string), Missing, Failed(Error))`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L74)
  Distinguishes a present value, a missing name, and a failed lookup.
- **Program.Environment** · [`export let get = func(name: string) -> Lookup`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L84)
  The value of an environment variable. Needs the env capability; without it the lookup fails with CapabilityDenied. Tagged `#NoDiscard`.
- **Program** · [`export let Process = module`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L95)
  Provides explicit termination of the current run.
- **Program.Process** · [`export let exit = func(status: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/program.nra#L103)
  Ends the run with `status` (0 through 255) and does not return. Returning from main is preferred.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 11.3 `Std.Program`

This module replaces `SysIo.Entry` ([section 4.6](Stdlib-Design#46-retired-built-ins)). It needs P3.

- **`Entry`** is a module with one requirement, `main(args: string[]) → ExitCode`. A program's entry module inherits it and overrides `main`, exactly as programs inherit `SysIo.Entry` today. Program arguments arrive only through this parameter.
- **`ExitCode`** is a payload enum: `Ok`, `Failure`, `Status(int)`, and the named statuses `SeriousFailure` (2), `CommandNotFound` (127), `IllegalInstruction` (132), `Breakpoint` (133), `Abort` (134), `ArithmeticError` (136), `OutOfMemory` (137), `BusError` (138), `SegmentationFault` (139), `CpuTimeExceeded` (152), and `FileSizeExceeded` (153). `Ok`, `Failure`, and `Status` map to 0, 1, and the given value. A `Status` outside 0 through 255 traps with `invalid argument: exit status`. The first plan kept the old one-variant enum; a payload enum lets a program return any status without a host call. Neura enums cannot carry explicit integer values, so the compiler and `status_of(code) → int` hold the table; the named variants follow declaration order after `Status`, so the original three keep their ordinals.
- **`status_of(code: ExitCode) → int`** returns the status a variant stands for, trapping for an invalid `Status`.
- **`Environment.get(name: string) → Lookup`**, where `Lookup` is a payload enum: `Found(string)`, `Missing`, and `Failed(Error)`. A purpose-built enum reads better here than a `Result` of an `Optional`. Needs P2.
- **`Process.exit(status: int)`** ends the run with that status through `neura.process.exit` and does not return. A status outside 0 through 255 traps. The overload `Process.exit(code: ExitCode)` exits with `status_of(code)`. Needs P2. Returning from `main` is preferred; `exit` is for leaving from deep inside a program.

### 18.2 Program, console, and testing

- **Entry.** `Std.Program` holds `ExitCode`, `Entry`, `Environment`, and `Process` as one module. A `Status` outside 0 through 255 traps with `invalid argument: exit status`.
- **Process exit.** `Process.exit` is available without a capability. A test that exits with a nonzero status fails.
- **Std.Test messages.** `assert_equal_float64` compares with float `equals` (NaN equals NaN, `0.0` differs from `-0.0`). `assert_none` and `assert_err` report `expected None, got Some` and `expected Err, got Ok` without the value's text, so they work for payload enums. `assert_not_equal` reports `expected a value other than <x>`.
- **Console.** Output is unbuffered. Input is read in 4,096-byte requests shared by `read_line` and `input()`. A write that makes no progress is an `Other` error.
